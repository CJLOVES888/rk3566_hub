/*
 * ============================================================================
 * detector.h —— NPU 检测工作类（独立 QThread）
 * ----------------------------------------------------------------------------
 * 输入：CameraWorker::frameReady → Detector::onFrame（排队连接）
 * 输出：detectionsUpdated / personDetected / statusChanged
 *
 * 跳帧：默认每 2 帧推理 1 次，降低 NPU+CPU 负载。
 * 人物事件：同一轮检测里 class_id==0 且 conf 超阈值，带冷却时间防刷屏。
 * ============================================================================
 */

#pragma once

#include <QObject>
#include <QImage>
#include <QVector>
#include <QString>
#include <atomic>

namespace vision {

struct DetectBox {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float conf = 0.f;
    int classId = -1;
};

class Detector : public QObject {
    Q_OBJECT

public:
    explicit Detector(QString modelPath,
                      float confThreshold = 0.5f,
                      QObject *parent = nullptr);
    ~Detector() override;

    void stop();

public slots:
    void start();                 /* 线程 started 后加载模型 */
    void onFrame(const QImage &frame);

signals:
    void detectionsUpdated(const QVector<vision::DetectBox> &boxes);
    void personDetected(float conf);          /* 冷却后上报 */
    void statusChanged(const QString &text);
    void errorOccurred(const QString &message);

private:
    QString m_modelPath;
    float m_confThreshold;
    int m_skip = 2;               /* 每 N 帧推 1 次 */
    int m_frameCounter = 0;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_busy{false};
    qint64 m_lastPersonMs = 0;
    int m_personCooldownMs = 2000;

    void *m_engine = nullptr;     /* rknn_engine_t*，避免头文件依赖 */
};

} // namespace vision

Q_DECLARE_METATYPE(vision::DetectBox)
Q_DECLARE_METATYPE(QVector<vision::DetectBox>)
