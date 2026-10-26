/*
 * ============================================================================
 * detector.cpp —— NPU 检测实现
 * ============================================================================
 */

#include "detector.h"
#include "rknn_engine.h"

#include <QDateTime>
#include <QDebug>

namespace vision {

Detector::Detector(QString modelPath, float confThreshold, QObject *parent)
    : QObject(parent)
    , m_modelPath(std::move(modelPath))
    , m_confThreshold(confThreshold)
{
}

Detector::~Detector()
{
    stop();
    if (m_engine) {
        rknn_engine_destroy(static_cast<rknn_engine_t *>(m_engine));
        m_engine = nullptr;
    }
}

void Detector::stop()
{
    m_running = false;
}

void Detector::start()
{
    m_running = true;
    emit statusChanged(QStringLiteral("NPU: 加载模型中..."));

    rknn_engine_t *eng = rknn_engine_create(m_modelPath.toUtf8().constData());
    if (!eng) {
        const QString err = QString::fromUtf8(rknn_engine_last_error());
        emit errorOccurred(QStringLiteral("NPU 初始化失败: %1").arg(err));
        emit statusChanged(QStringLiteral("NPU: 未就绪"));
        return;
    }
    m_engine = eng;
    emit statusChanged(QStringLiteral("NPU: 就绪"));
    qInfo() << "Detector model loaded:" << m_modelPath;
}

void Detector::onFrame(const QImage &frame)
{
    if (!m_running.load() || !m_engine) {
        return;
    }
    if (m_busy.exchange(true)) {
        return; /* 上一帧还在推，丢弃避免堆积 */
    }

    ++m_frameCounter;
    if (m_skip > 1 && (m_frameCounter % m_skip) != 0) {
        m_busy = false;
        return;
    }

    QImage rgb = frame;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    rknn_detect_box_t raw[RKNN_ENGINE_MAX_BOXES];
    const int n = rknn_engine_detect(
        static_cast<rknn_engine_t *>(m_engine),
        rgb.constBits(), rgb.width(), rgb.height(),
        m_confThreshold, 0.45f,
        raw, RKNN_ENGINE_MAX_BOXES);

    if (n < 0) {
        emit errorOccurred(QString::fromUtf8(rknn_engine_last_error()));
        m_busy = false;
        return;
    }

    QVector<DetectBox> boxes;
    boxes.reserve(n);
    float bestPerson = 0.f;
    for (int i = 0; i < n; ++i) {
        DetectBox b;
        b.x1 = raw[i].x1;
        b.y1 = raw[i].y1;
        b.x2 = raw[i].x2;
        b.y2 = raw[i].y2;
        b.conf = raw[i].conf;
        b.classId = raw[i].class_id;
        boxes.push_back(b);
        if (b.classId == 0 && b.conf > bestPerson) {
            bestPerson = b.conf;
        }
    }

    emit detectionsUpdated(boxes);

    const int64_t us = rknn_engine_last_infer_us(static_cast<rknn_engine_t *>(m_engine));
    emit statusChanged(QStringLiteral("NPU: %1ms | boxes=%2")
                           .arg(us > 0 ? us / 1000.0 : 0.0, 0, 'f', 1)
                           .arg(n));

    if (bestPerson >= m_confThreshold) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastPersonMs >= m_personCooldownMs) {
            m_lastPersonMs = now;
            emit personDetected(bestPerson);
        }
    }

    m_busy = false;
}

} // namespace vision
