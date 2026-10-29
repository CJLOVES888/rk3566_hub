# -*- coding: utf-8 -*-
"""Generate Qt and C++17/STL handbooks tailored to rk3566_hub."""

from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
import datetime
import os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def set_run_font(run, name_cn="宋体", name_en="Times New Roman", size=12, bold=False, color=None):
    run.bold = bold
    run.font.size = Pt(size)
    if color:
        run.font.color.rgb = color
    run.font.name = name_en
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.get_or_add_rFonts()
    rFonts.set(qn("w:eastAsia"), name_cn)


def add_heading_cn(doc, text, level=1):
    h = doc.add_heading(text, level=level)
    for run in h.runs:
        if level == 1:
            set_run_font(run, "黑体", "Arial", 16, True, RGBColor(0x1A, 0x56, 0x8C))
        elif level == 2:
            set_run_font(run, "黑体", "Arial", 14, True, RGBColor(0x2E, 0x75, 0xB6))
        else:
            set_run_font(run, "黑体", "Arial", 12, True, RGBColor(0x33, 0x33, 0x33))
    return h


def add_para(doc, text, size=12, bold=False, indent=True, align="justify"):
    p = doc.add_paragraph()
    if align == "center":
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    elif align == "left":
        p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    else:
        p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    pf = p.paragraph_format
    pf.line_spacing_rule = WD_LINE_SPACING.ONE_POINT_FIVE
    pf.space_after = Pt(6)
    if indent:
        pf.first_line_indent = Cm(0.74)
    run = p.add_run(text)
    set_run_font(run, "宋体", "Times New Roman", size, bold)
    return p


def add_bullet(doc, text):
    p = doc.add_paragraph(style="List Bullet")
    p.clear()
    p.paragraph_format.line_spacing_rule = WD_LINE_SPACING.ONE_POINT_FIVE
    p.paragraph_format.left_indent = Cm(0.74)
    run = p.add_run(text)
    set_run_font(run, "宋体", "Times New Roman", 12)
    return p


def add_code(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.4)
    p.paragraph_format.space_after = Pt(8)
    p.paragraph_format.line_spacing_rule = WD_LINE_SPACING.SINGLE
    run = p.add_run(text)
    set_run_font(run, "Consolas", "Consolas", 9.5, False, RGBColor(0x22, 0x22, 0x22))
    return p


def shade_cell(cell, hex_color):
    tc = cell._tc
    tcPr = tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:fill"), hex_color)
    shd.set(qn("w:val"), "clear")
    tcPr.append(shd)


def set_cell(cell, text, bold=False, size=10.5):
    cell.text = ""
    p = cell.paragraphs[0]
    run = p.add_run(text)
    set_run_font(run, "宋体", "Times New Roman", size, bold)


def add_table(doc, headers, rows):
    table = doc.add_table(rows=1 + len(rows), cols=len(headers))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    for i, h in enumerate(headers):
        set_cell(table.rows[0].cells[i], h, bold=True)
        shade_cell(table.rows[0].cells[i], "D6E3F0")
    for r_i, row in enumerate(rows):
        for c_i, val in enumerate(row):
            set_cell(table.rows[r_i + 1].cells[c_i], str(val))
    doc.add_paragraph()
    return table


def cover(doc, title, subtitle):
    for _ in range(2):
        doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run(title)
    set_run_font(run, "黑体", "Arial", 22, True, RGBColor(0x1A, 0x56, 0x8C))
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run(subtitle)
    set_run_font(run, "黑体", "Arial", 14, True)
    doc.add_paragraph()
    for line in [
        "配套工程：rk3566_hub（书桌/门口值守终端）",
        "用途：按项目真实用法快速上手，不讲用不到的大而全",
        "日期：" + datetime.date.today().strftime("%Y年%m月%d日"),
    ]:
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(line)
        set_run_font(run, "宋体", "Times New Roman", 12)
    doc.add_page_break()


# =============================================================================
# Qt handbook
# =============================================================================
def build_qt():
    doc = Document()
    for s in doc.sections:
        s.top_margin = Cm(2.5)
        s.bottom_margin = Cm(2.5)
        s.left_margin = Cm(2.5)
        s.right_margin = Cm(2.5)

    cover(doc, "Qt5 知识手册（项目定制版）", "围绕 rk3566_hub 实际代码")

    add_heading_cn(doc, "目录", 1)
    for t in [
        "1 你在本项目里用 Qt 做什么",
        "2 程序骨架：QApplication 与 MainWindow",
        "3 控件与布局（你每天看到的界面）",
        "4 信号与槽（最重要）",
        "5 多线程：摄像头为什么不卡 UI",
        "6 字符串、容器、时间、环境变量",
        "7 JSON 与 MQTT 消息",
        "8 图像：QImage / QPixmap",
        "9 QtSql + SQLite",
        "10 平台插件 linuxfb / xcb",
        "11 一周速记清单",
        "附录：对照源文件",
    ]:
        add_para(doc, t, indent=False, align="left")
    doc.add_page_break()

    add_heading_cn(doc, "1 你在本项目里用 Qt 做什么", 1)
    add_para(
        doc,
        "本项目不是「学完整 Qt」，而是用 Qt5 做嵌入式界面壳子 + 线程调度 + 本地数据库。"
        "核心模块：MainWindow（编排）、CameraWorker（采集线程）、PresenceGuard（值守）、"
        "MqttClient（网络，底层 libmosquitto）、Database（SQLite）。",
    )
    add_table(
        doc,
        ["模块", "主要 Qt 能力"],
        [
            ["main.cpp", "QApplication、全屏几何"],
            ["MainWindow", "控件布局、槽函数、定时器、JSON、启停子模块"],
            ["CameraWorker", "QObject 进 QThread、发 frameReady 信号"],
            ["PresenceGuard", "收帧、算占用、发 presenceNear/Away"],
            ["Database", "QSqlDatabase / QSqlQuery"],
            ["protocol.h", "QJsonDocument / QJsonObject"],
        ],
    )

    add_heading_cn(doc, "2 程序骨架：QApplication 与 MainWindow", 1)
    add_para(doc, "每个 Qt Widgets 程序几乎都是：")
    add_code(
        doc,
        "int main(int argc, char *argv[]) {\n"
        "    QApplication app(argc, argv);   // 事件循环管家\n"
        "    ui::MainWindow window;\n"
        "    window.show();\n"
        "    return app.exec();              // 阻塞，处理点击/定时器/信号\n"
        "}",
    )
    add_bullet(doc, "没有 app.exec()，界面画出来也会立刻退出。")
    add_bullet(doc, "MainWindow 继承 QWidget：一块可布局的窗口区域。")
    add_bullet(doc, "构造函数里：buildUi → startPresence → startCamera → startMqtt。")

    add_heading_cn(doc, "3 控件与布局（你每天看到的界面）", 1)
    add_heading_cn(doc, "3.1 常用控件", 2)
    add_table(
        doc,
        ["类", "项目用途"],
        [
            ["QLabel", "标题、视频、MQTT/值守/NPU 状态、时钟"],
            ["QPushButton", "「上报测试事件」"],
            ["QTimer", "每秒刷新时钟；标题「有人靠近」几秒后恢复"],
            ["QStackedLayout", "预览页 / 息屏黑页切换（screen_on/off）"],
            ["QVBoxLayout", "从上到下排控件"],
        ],
    )
    add_heading_cn(doc, "3.2 布局直觉", 2)
    add_para(
        doc,
        "Layout 负责自动拉伸。视频区用 stretch=1 占满剩余高度；"
        "setPixmap(...scaled(label->size(), KeepAspectRatio)) 保持画面比例。",
    )
    add_code(
        doc,
        "auto *layout = new QVBoxLayout(this);\n"
        "layout->addWidget(m_titleLabel);\n"
        "layout->addWidget(stackHost, 1);   // 1 = 吃掉剩余空间\n"
        "layout->addWidget(m_statusLabel);",
    )

    add_heading_cn(doc, "4 信号与槽（最重要）", 1)
    add_para(
        doc,
        "信号（signal）=「发生了某事」；槽（slot）=「收到后做什么」。"
        "用 connect 绑定。本项目大量跨线程投递画面和 MQTT 事件。",
    )
    add_code(
        doc,
        "connect(m_cameraWorker, &vision::CameraWorker::frameReady,\n"
        "        this, &MainWindow::onFrame);\n"
        "// 摄像头线程发出 frameReady → UI 线程执行 onFrame",
    )
    add_bullet(doc, "类里要有 Q_OBJECT 宏，才能用信号槽（需 AUTOMOC）。")
    add_bullet(doc, "自定义类型跨线程传：qRegisterMetaType（如 DetectBox）。")
    add_bullet(doc, "QueuedConnection：跨线程排队到接收者线程执行（默认跨线程就是它）。")
    add_heading_cn(doc, "4.1 项目中的关键连接", 2)
    add_table(
        doc,
        ["信号", "槽", "含义"],
        [
            ["CameraWorker::frameReady", "MainWindow::onFrame", "刷新预览 + 喂给值守"],
            ["PresenceGuard::presenceNear", "onPresenceNear", "亮屏 + MQTT"],
            ["PresenceGuard::presenceAway", "onPresenceAway", "息屏 + MQTT"],
            ["MqttClient::messageReceived", "onMqttMessage", "解析指令"],
            ["MqttClient::connected", "onMqttConnected", "更新状态并上报 online"],
            ["QPushButton::clicked", "onPublishTestClicked", "手动发测试事件"],
        ],
    )

    add_heading_cn(doc, "5 多线程：摄像头为什么不卡 UI", 1)
    add_para(
        doc,
        "采帧是死循环，若放在 UI 线程，界面会卡死。做法：Worker 继承 QObject，"
        "moveToThread 到 QThread，started 后调用 start()。",
    )
    add_code(
        doc,
        "m_cameraWorker = new vision::CameraWorker(-1, 640, 480);\n"
        "m_cameraWorker->moveToThread(&m_cameraThread);\n"
        "connect(&m_cameraThread, &QThread::started,\n"
        "        m_cameraWorker, &vision::CameraWorker::start);\n"
        "m_cameraThread.start();",
    )
    add_bullet(doc, "止停：worker->stop()；thread.quit(); thread.wait();")
    add_bullet(doc, "息屏时仍要把帧送给 PresenceGuard，否则无法「来人亮屏」。")
    add_bullet(doc, "MqttClient 用 mosquitto_loop_start 自带网络线程，回调里再 invokeMethod 回 Qt 线程。")

    add_heading_cn(doc, "6 字符串、容器、时间、环境变量", 1)
    add_heading_cn(doc, "6.1 QString", 2)
    add_para(doc, "Qt 里几乎所有文本都是 QString（Unicode）。常用：")
    add_code(
        doc,
        "QStringLiteral(\"值守: 监测中\");          // 编译期字面量，推荐\n"
        "QString::number(fps, 'f', 1);\n"
        "QString(\"a %1 b %2\").arg(x).arg(y);   // 不要用 printf 的 %.1f\n"
        "s.contains(\"camera\"); s.trimmed();",
    )
    add_para(
        doc,
        "注意：QString::arg 用 %1 %2；百分号写成 %% 。项目里曾误写 %.1f%% 导致界面原样显示。",
        bold=False,
    )
    add_heading_cn(doc, "6.2 容器", 2)
    add_table(
        doc,
        ["类型", "用途"],
        [
            ["QStringList", "GStreamer 管道列表、设备候选"],
            ["QVector<DetectBox>", "检测框（NPU 预留）"],
            ["QProcessEnvironment", "读 HUB_MQTT_UID 等环境变量"],
        ],
    )
    add_heading_cn(doc, "6.3 时间", 2)
    add_code(
        doc,
        "QDateTime::currentDateTime().toString(\"yyyy-MM-dd HH:mm:ss\");\n"
        "QDateTime::currentMSecsSinceEpoch();   // 值守冷却、防抖\n"
        "QTimer::singleShot(2500, this, [this]{ ... });",
    )

    add_heading_cn(doc, "7 JSON 与 MQTT 消息", 1)
    add_para(doc, "协议在 net/protocol.h：用 QJsonObject 组装，再 QJsonDocument 序列化。")
    add_code(
        doc,
        "QJsonObject payload;\n"
        "payload.insert(\"action\", \"person_near\");\n"
        "payload.insert(\"device_id\", m_deviceId);\n"
        "QByteArray msg = net::buildMessage(\"event\", payload);\n"
        "m_mqtt->publishEvent(msg, 1);",
    )
    add_bullet(doc, "收包：先 parseMessage；失败则把整段 payload 当纯文本 action（兼容 screen_off）。")
    add_bullet(doc, "QByteArray = 原始字节，适合 MQTT 载荷。")

    add_heading_cn(doc, "8 图像：QImage / QPixmap", 1)
    add_para(
        doc,
        "摄像头线程产出 QImage（RGB888）→ UI 用 QPixmap::fromImage 显示。"
        "OpenCV Mat 转 QImage 时要注意步进 step，并 copy() 防止底层缓冲被下一帧覆盖。",
    )
    add_code(
        doc,
        "QImage img(rgb.data, w, h, step, QImage::Format_RGB888);\n"
        "emit frameReady(img.copy());\n"
        "// UI:\n"
        "m_videoLabel->setPixmap(QPixmap::fromImage(shown).scaled(\n"
        "    m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));",
    )

    add_heading_cn(doc, "9 QtSql + SQLite", 1)
    add_para(doc, "storage/Database 单例：open → ensureSchema → seedDefaults。")
    add_code(
        doc,
        "QSqlDatabase db = QSqlDatabase::addDatabase(\"QSQLITE\", name);\n"
        "db.setDatabaseName(path);\n"
        "db.open();\n"
        "QSqlQuery q(db);\n"
        "q.prepare(\"INSERT INTO event_log(ts,type,detail,source) VALUES(?,?,?,?)\");\n"
        "q.addBindValue(...); q.exec();",
    )
    add_bullet(doc, "业务侧只调 Database::instance().logEvent(...) / getConfig(...)。")
    add_bullet(doc, "库文件：build/bin/data/smart_hub.db")

    add_heading_cn(doc, "10 平台插件 linuxfb / xcb", 1)
    add_para(
        doc,
        "嵌入式无桌面时用 QT_QPA_PLATFORM=linuxfb 直写 /dev/fb0；"
        "有 XFCE 时 linuxfb 会被挡住，调试用 DISPLAY=:0 + xcb。"
        "run.sh 已自动选择；全屏独占需先 stop lightdm。",
    )

    add_heading_cn(doc, "11 一周速记清单", 1)
    add_bullet(doc, "QApplication::exec = 事件循环")
    add_bullet(doc, "connect(发送者, 信号, 接收者, 槽)")
    add_bullet(doc, "耗时循环 → moveToThread，结果用信号抛回 UI")
    add_bullet(doc, "QStringLiteral + arg(%1)，别混 printf")
    add_bullet(doc, "QImage 跨线程务必 copy")
    add_bullet(doc, "息屏仍要处理帧（值守）")
    add_bullet(doc, "配置优先环境变量，其次 SQLite config 表")

    add_heading_cn(doc, "附录：对照源文件", 1)
    add_table(
        doc,
        ["想学的点", "打开文件"],
        [
            ["界面与编排", "src/ui/main_window.cpp"],
            ["信号槽与线程", "main_window.cpp 的 startCamera"],
            ["值守逻辑", "src/vision/presence_guard.cpp"],
            ["JSON 协议", "src/net/protocol.h"],
            ["SQLite", "src/storage/database.cpp"],
            ["入口", "src/main.cpp"],
        ],
    )
    add_para(doc, "—— Qt 手册结束 ——", indent=False, align="center")

    path = os.path.join(OUT_DIR, "Qt5知识手册_rk3566_hub项目定制版.docx")
    doc.save(path)
    return path


# =============================================================================
# C++17 / STL handbook
# =============================================================================
def build_cpp():
    doc = Document()
    for s in doc.sections:
        s.top_margin = Cm(2.5)
        s.bottom_margin = Cm(2.5)
        s.left_margin = Cm(2.5)
        s.right_margin = Cm(2.5)

    cover(doc, "C++17 / STL 知识手册（项目定制版）", "只覆盖 rk3566_hub 用到的部分")

    add_heading_cn(doc, "目录", 1)
    for t in [
        "1 学习目标：够读本项目代码即可",
        "2 编译与语言档位",
        "3 类、命名空间、头文件习惯",
        "4 指针、引用、const（嵌入式必会）",
        "5 STL 容器：vector 等",
        "6 智能与原子：atomic",
        "7 算法与工具函数",
        "8 Lambda（配合 Qt 定时器）",
        "9 C++17 你项目里相关的点",
        "10 与 C 混编（rknn_engine / V4L2）",
        "11 常见坑（对照本项目）",
        "12 练习建议",
        "附录：符号对照表",
    ]:
        add_para(doc, t, indent=False, align="left")
    doc.add_page_break()

    add_heading_cn(doc, "1 学习目标：够读本项目代码即可", 1)
    add_para(
        doc,
        "你暂时不需要精通全部 STL 与模板元编程。目标是：能读懂 MainWindow / CameraWorker / "
        "V4l2MplaneCapture / PresenceGuard，并能改逻辑、加日志、修编译错误。"
        "本手册按「项目出现什么就讲什么」编写。",
    )

    add_heading_cn(doc, "2 编译与语言档位", 1)
    add_bullet(doc, "CMake：CMAKE_CXX_STANDARD 17")
    add_bullet(doc, "业务代码多为 C++；rknn_engine.c、部分系统调用为 C")
    add_bullet(doc, "板端：cmake .. && make -j$(nproc)")

    add_heading_cn(doc, "3 类、命名空间、头文件习惯", 1)
    add_code(
        doc,
        "namespace vision {\n"
        "class CameraWorker : public QObject {\n"
        "    Q_OBJECT\n"
        "public:\n"
        "    explicit CameraWorker(...);\n"
        "public slots:\n"
        "    void start();\n"
        "signals:\n"
        "    void frameReady(const QImage &frame);\n"
        "};\n"
        "} // namespace vision",
    )
    add_bullet(doc, "namespace ui / net / vision / storage：按模块分区，避免重名。")
    add_bullet(doc, "头文件 #pragma once；实现放 .cpp。")
    add_bullet(doc, "explicit 构造：防止隐式转换坑。")
    add_bullet(doc, "override：标记重写虚函数（析构 ~MainWindow() override）。")

    add_heading_cn(doc, "4 指针、引用、const（嵌入式必会）", 1)
    add_table(
        doc,
        ["写法", "含义", "项目例子"],
        [
            ["T*", "可空指针，指向对象", "CameraWorker *m_cameraWorker"],
            ["T&", "别名，不能空", "const QImage &frame"],
            ["const T&", "只读引用，避免拷贝大对象", "onFrame(const QImage &frame)"],
            ["T* = nullptr", "成员默认空", "MqttClient *m_mqtt = nullptr"],
        ],
    )
    add_para(
        doc,
        "大对象（QImage、cv::Mat、QString 较长时）函数参数优先 const&。"
        "跨线程传 QImage 时项目里用 img.copy()，避免悬空缓冲。",
    )
    add_heading_cn(doc, "4.1 new / delete 与 Qt 父子所有权", 2)
    add_para(
        doc,
        "Qt 中 new QLabel(this) 的 parent 会在父对象析构时自动删子对象。"
        "摄像头 Worker 需要自己 delete；线程要 quit/wait。看 MainWindow 析构。",
    )

    add_heading_cn(doc, "5 STL 容器：vector 等", 1)
    add_heading_cn(doc, "5.1 std::vector（本项目最常用 STL）", 2)
    add_para(
        doc,
        "v4l2_mplane_capture.cpp 里用 std::vector<uchar> 拼 NV12 紧凑缓冲；"
        "检测框侧多用 Qt 的 QVector（用法很像 vector）。",
    )
    add_code(
        doc,
        "#include <vector>\n"
        "std::vector<uchar> packed(width * height * 3 / 2);\n"
        "uchar *dst = packed.data();\n"
        "memcpy(dst + row * width, src + row * stride, width);",
    )
    add_bullet(doc, "size() / empty() / data() / push_back() / operator[]")
    add_bullet(doc, "连续内存，适合给 C API（OpenCV、V4L2）当缓冲区")
    add_heading_cn(doc, "5.2 和 Qt 容器怎么选", 2)
    add_table(
        doc,
        ["场景", "建议"],
        [
            ["纯 Qt 信号槽、UI", "QString / QVector / QStringList"],
            ["与 C/OpenCV/系统 API 交互的字节缓冲", "std::vector<uint8_t/uchar>"],
            ["固定小数组", "C 数组或 std::array（项目里 planes[2]）"],
        ],
    )

    add_heading_cn(doc, "6 智能与原子：atomic", 1)
    add_para(
        doc,
        "CameraWorker / PresenceGuard / Detector 用 std::atomic<bool> m_running。"
        "多线程下普通 bool 读写可能有可见性问题；atomic 保证线程间可靠读写。",
    )
    add_code(
        doc,
        "#include <atomic>\n"
        "std::atomic<bool> m_running{false};\n"
        "void stop() { m_running = false; }\n"
        "while (m_running.load()) { ... }",
    )
    add_bullet(doc, "本项目几乎不用 mutex：通过「原子标志 + Qt 信号排队」降低锁复杂度。")
    add_bullet(doc, "暂时不必深究 memory_order；默认即可。")

    add_heading_cn(doc, "7 算法与工具函数", 1)
    add_bullet(doc, "<cstring>：memcpy、strerror（V4L2 错误信息）")
    add_bullet(doc, "<cmath> / std::abs：PresenceGuard 像素差")
    add_bullet(doc, "qMax / qBound / qAbs：Qt 提供的数值工具（idleMs 下限等）")
    add_code(
        doc,
        "m_cfg.idleMs = qMax(3000, ms);\n"
        "m_cfg.motionRatio = qBound(0.005, ratio, 0.5);",
    )

    add_heading_cn(doc, "8 Lambda（配合 Qt 定时器）", 1)
    add_para(doc, "匿名函数对象，常捕获 this，用于延时恢复标题：")
    add_code(
        doc,
        "QTimer::singleShot(2500, this, [this]() {\n"
        "    m_titleLabel->setText(QStringLiteral(\"RK3566 书桌/门口值守\"));\n"
        "});",
    )
    add_bullet(doc, "[this]：用成员；注意对象生命周期，挂在 this 上较安全。")
    add_bullet(doc, "MqttClient 回调里也有 lambda + QMetaObject::invokeMethod。")

    add_heading_cn(doc, "9 C++17 你项目里相关的点", 1)
    add_table(
        doc,
        ["特性", "你需要知道的程度"],
        [
            ["结构化绑定", "可暂缓；代码里几乎未用"],
            ["if 初始化语句", "可暂缓"],
            ["std::optional", "未作为主路径；看到再学"],
            ["枚举 class", "PixelPath : 强类型枚举，推荐"],
            ["nullptr", "代替 NULL"],
            ["auto", "局部变量可省类型，别滥用到看不懂"],
            ["范围 for", "for (const QString &dev : devices)"],
        ],
    )
    add_code(
        doc,
        "enum class PixelPath { BgrMat, RgbMat, Nv12, Nv21, Yuyv2, Gray };\n"
        "for (const QString &dev : devices) {\n"
        "    if (!dev.startsWith('/')) continue;\n"
        "    ...\n"
        "}",
    )

    add_heading_cn(doc, "10 与 C 混编（rknn_engine / V4L2）", 1)
    add_para(
        doc,
        "Linux 头文件、ioctl、mmap 是 C API。C++ 中直接 #include <linux/videodev2.h> 即可。"
        "RKNN 引擎用 C 编写，头文件用 extern \"C\" 包起来供 C++ 链接。",
    )
    add_code(
        doc,
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "rknn_engine_t *rknn_engine_create(const char *model_path);\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif",
    )
    add_bullet(doc, "C 没有类/引用；用指针和返回值码表示错误。")
    add_bullet(doc, "vector.data() 得到连续 uchar* 可传给 C 函数。")

    add_heading_cn(doc, "11 常见坑（对照本项目）", 1)
    add_bullet(doc, "悬挂指针：QImage 未 copy 就跨线程用 → 花屏/崩溃")
    add_bullet(doc, "忘记原子/止停标志：线程 join 不住")
    add_bullet(doc, "混用 printf 格式和 QString::arg")
    add_bullet(doc, "头文件互相包含：前向声明 struct/class 打断循环依赖（mqtt 里 mosquitto 前向声明）")
    add_bullet(doc, "override / 析构顺序：先停 MQTT/检测/摄像头再关库")

    add_heading_cn(doc, "12 练习建议", 1)
    add_bullet(doc, "读懂 PresenceGuard::onFrame，改 idle 秒数与阈值，观察行为")
    add_bullet(doc, "给 logEvent 多加一种 type，用 sqlite3 查出来")
    add_bullet(doc, "给 MainWindow 加一个 QLabel 显示今日 person_near 次数（SELECT COUNT）")
    add_bullet(doc, "把一段 for 循环改成范围 for，确认编译通过")
    add_bullet(doc, "解释：为什么 CameraWorker::start 不能直接在 UI 线程死循环调用")

    add_heading_cn(doc, "附录：符号对照表", 1)
    add_table(
        doc,
        ["你在代码里看到", "一句话"],
        [
            ["std::vector", "动态数组"],
            ["std::atomic<bool>", "线程安全的布尔开关"],
            ["enum class", "作用域枚举，更安全"],
            ["nullptr", "空指针"],
            ["const T&", "只读引用"],
            ["override", "确认重写了基类虚函数"],
            ["= delete", "禁止拷贝（Database 单例）"],
            ["lambda [this](){}", "内联回调函数"],
            ["reinterpret/强制指针", "V4L2 mmap 缓冲当 uchar* 用"],
        ],
    )
    add_para(doc, "—— C++17/STL 手册结束 ——", indent=False, align="center")

    path = os.path.join(OUT_DIR, "C++17与STL知识手册_rk3566_hub项目定制版.docx")
    doc.save(path)
    return path


if __name__ == "__main__":
    p1 = build_qt()
    p2 = build_cpp()
    print("OK", p1)
    print("OK", p2)
    print("size", os.path.getsize(p1), os.path.getsize(p2))
