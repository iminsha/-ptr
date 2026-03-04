#include "mainwindow.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QRandomGenerator>
#include <QVBoxLayout>
#include <QComboBox>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QStringList>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <signal.h>

namespace {
constexpr bool kDirectTcpMode = true;
const char *kSocatPidFile = "/tmp/mysmart_socat.pid";
constexpr unsigned char kProtoVer = 0x01;
constexpr unsigned char kCmdPingReq = 0x01;
constexpr unsigned char kCmdModeSetReq = 0x02;
constexpr unsigned char kCmdMoveToReq = 0x03;
constexpr unsigned char kCmdStopReq = 0x04;
constexpr unsigned char kCmdBeepReq = 0x05;
constexpr unsigned char kCmdCfgGetReq = 0x06;
constexpr unsigned char kCmdStatusGetReq = 0x08;
constexpr unsigned char kCmdStatusRsp = 0x48;
constexpr unsigned char kCmdAck = 0x50;
constexpr unsigned char kCmdNack = 0x51;
constexpr unsigned char kCmdStatusEvt = 0x80;

unsigned char protocolChecksum(unsigned char cmd, unsigned char seq, const QByteArray &payload)
{
    unsigned int sum = 0;
    int i = 0;
    sum += kProtoVer;
    sum += cmd;
    sum += seq;
    sum += static_cast<unsigned char>(payload.size());
    for (i = 0; i < payload.size(); ++i)
    {
        sum += static_cast<unsigned char>(payload.at(i));
    }
    return static_cast<unsigned char>(sum & 0xFFu);
}

void saveSocatPid(qint64 pid)
{
    QFile f(QString::fromLatin1(kSocatPidFile));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        QTextStream out(&f);
        out << pid << "\n";
    }
}

void clearSocatPid()
{
    QFile::remove(QString::fromLatin1(kSocatPidFile));
}

void killStaleSocatFromPidFile()
{
    QFile f(QString::fromLatin1(kSocatPidFile));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }

    QTextStream in(&f);
    bool ok = false;
    const qint64 pid = in.readLine().trimmed().toLongLong(&ok);
    if (!ok || pid <= 1)
    {
        clearSocatPid();
        return;
    }

    if (::kill(static_cast<pid_t>(pid), 0) == 0)
    {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    }
    clearSocatPid();
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    modeBox_ = nullptr;
    clockLabel_ = nullptr;
    tempLabel_ = nullptr;
    actuatorLabel_ = nullptr;
    errorLabel_ = nullptr;
    thHigh_ = nullptr;
    thLow_ = nullptr;
    cooldown_ = nullptr;
    defaultMode_ = nullptr;
    scheduleTable_ = nullptr;
    logView_ = nullptr;
    mockTimer_ = nullptr;
    serial_ = nullptr;
    portBox_ = nullptr;
    baudBox_ = nullptr;
    openBtn_ = nullptr;
    txEdit_ = nullptr;
    linkLabel_ = nullptr;
    linkTypeBox_ = nullptr;
    tcpHostEdit_ = nullptr;
    tcpPortSpin_ = nullptr;
    tcp_ = nullptr;
    socatProcess_ = nullptr;

    buildUi();

    serial_ = new QSerialPort(this);
    connect(serial_, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);
    tcp_ = new QTcpSocket(this);
    connect(tcp_, &QTcpSocket::readyRead, this, &MainWindow::onTcpReadyRead);
    connect(tcp_, &QTcpSocket::connected, this, &MainWindow::onTcpConnected);
    connect(tcp_, &QTcpSocket::disconnected, this, &MainWindow::onTcpDisconnected);
    connect(tcp_, &QTcpSocket::errorOccurred, this, &MainWindow::onTcpError);

    mockTimer_ = new QTimer(this);
    connect(mockTimer_, &QTimer::timeout, this, &MainWindow::onMockTick);
    mockTimer_->start(500);

    onRefreshPorts();
    if (linkTypeBox_)
    {
        linkTypeBox_->setCurrentText(kDirectTcpMode ? "TCP" : "Serial");
    }
    if (portBox_)
    {
        portBox_->setCurrentText("/tmp/ttyV0");
    }
    if (baudBox_)
    {
        baudBox_->setCurrentText("4800");
    }

    appendLog("UI ready. Direct TCP mode.");
    appendLog("Expected chain: MCU UART -> Windows TCP Server -> Linux UI(TCP)");

    QTimer::singleShot(0, this, [this]() {
        if (!tcp_ || !tcpHostEdit_ || !tcpPortSpin_)
        {
            return;
        }
        tcp_->connectToHost(tcpHostEdit_->text().trimmed(), static_cast<quint16>(tcpPortSpin_->value()));
    });
}

MainWindow::~MainWindow()
{
    if (tcp_ && tcp_->state() != QAbstractSocket::UnconnectedState)
    {
        tcp_->disconnectFromHost();
        tcp_->waitForDisconnected(500);
    }
}

void MainWindow::buildUi()
{
    setWindowTitle("mySmart Host");
    resize(1200, 780);

    auto *root = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(root);

    auto *serialBox = new QGroupBox("Serial", root);
    auto *serialRow = new QHBoxLayout(serialBox);

    portBox_ = new QComboBox(serialBox);
    portBox_->setEditable(true);
    portBox_->setInsertPolicy(QComboBox::NoInsert);
    portBox_->addItems({"/tmp/ttyV0", "/tmp/ttyUSB0", "/tmp/ttyACM0"});
    portBox_->setCurrentText("/tmp/ttyV0");
    baudBox_ = new QComboBox(serialBox);
    linkTypeBox_ = new QComboBox(serialBox);
    tcpHostEdit_ = new QLineEdit(serialBox);
    tcpPortSpin_ = new QSpinBox(serialBox);
    baudBox_->addItems({"4800", "9600", "19200", "38400", "57600", "115200"});
    baudBox_->setCurrentText("4800");
    linkTypeBox_->addItems({"Serial", "TCP"});
    if (kDirectTcpMode)
    {
        linkTypeBox_->setCurrentText("TCP");
        linkTypeBox_->setEnabled(false);
    }
    tcpHostEdit_->setText("192.168.159.1");
    tcpPortSpin_->setRange(1, 65535);
    tcpPortSpin_->setValue(8888);

    auto *refreshBtn = new QPushButton("Refresh", serialBox);
    openBtn_ = new QPushButton("Open", serialBox);
    txEdit_ = new QLineEdit(serialBox);
    auto *sendBtn = new QPushButton("Send", serialBox);
    linkLabel_ = new QLabel("Closed", serialBox);

    txEdit_->setPlaceholderText("Type command, e.g. PING / BEEP1 / GETCFG");

    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::onTogglePort);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSendLine);
    connect(linkTypeBox_, &QComboBox::currentTextChanged, this, &MainWindow::onLinkTypeChanged);

    serialRow->addWidget(new QLabel("Link:"));
    serialRow->addWidget(linkTypeBox_);
    serialRow->addWidget(new QLabel("Port:"));
    serialRow->addWidget(portBox_);
    serialRow->addWidget(new QLabel("Baud:"));
    serialRow->addWidget(baudBox_);
    serialRow->addWidget(new QLabel("TCP Host:"));
    serialRow->addWidget(tcpHostEdit_);
    serialRow->addWidget(new QLabel("TCP Port:"));
    serialRow->addWidget(tcpPortSpin_);
    serialRow->addWidget(refreshBtn);
    serialRow->addWidget(openBtn_);
    serialRow->addWidget(txEdit_, 1);
    serialRow->addWidget(sendBtn);
    serialRow->addWidget(new QLabel("State:"));
    serialRow->addWidget(linkLabel_);

    auto *topRow = new QHBoxLayout();

    auto *statusBox = new QGroupBox("Runtime Status", root);
    auto *statusGrid = new QGridLayout(statusBox);

    modeBox_ = new QComboBox(statusBox);
    modeBox_->addItems({"AUTO", "MANUAL", "SAFE"});
    connect(modeBox_, &QComboBox::currentTextChanged, this, &MainWindow::onModeChanged);

    clockLabel_ = new QLabel("--:--:--", statusBox);
    tempLabel_ = new QLabel("--.- C", statusBox);
    actuatorLabel_ = new QLabel("0 %", statusBox);
    errorLabel_ = new QLabel("NONE", statusBox);

    statusGrid->addWidget(new QLabel("Mode:"), 0, 0);
    statusGrid->addWidget(modeBox_, 0, 1);
    statusGrid->addWidget(new QLabel("RTC:"), 1, 0);
    statusGrid->addWidget(clockLabel_, 1, 1);
    statusGrid->addWidget(new QLabel("Temperature:"), 2, 0);
    statusGrid->addWidget(tempLabel_, 2, 1);
    statusGrid->addWidget(new QLabel("Actuator:"), 3, 0);
    statusGrid->addWidget(actuatorLabel_, 3, 1);
    statusGrid->addWidget(new QLabel("Error:"), 4, 0);
    statusGrid->addWidget(errorLabel_, 4, 1);

    auto *controlBox = new QGroupBox("Action Center", root);
    auto *controlLayout = new QGridLayout(controlBox);

    auto mkBtn = [this, controlBox](const QString &text) {
        auto *btn = new QPushButton(text, controlBox);
        connect(btn, &QPushButton::clicked, this, &MainWindow::onActionClicked);
        return btn;
    };

    controlLayout->addWidget(mkBtn("MOVE 10%"), 0, 0);
    controlLayout->addWidget(mkBtn("MOVE 30%"), 0, 1);
    controlLayout->addWidget(mkBtn("MOVE 70%"), 0, 2);
    controlLayout->addWidget(mkBtn("MOVE 100%"), 0, 3);
    controlLayout->addWidget(mkBtn("STOP"), 1, 0);
    controlLayout->addWidget(mkBtn("BEEP x1"), 1, 1);
    controlLayout->addWidget(mkBtn("BEEP x3"), 1, 2);
    controlLayout->addWidget(mkBtn("CLEAR ERR"), 1, 3);

    topRow->addWidget(statusBox, 2);
    topRow->addWidget(controlBox, 3);

    auto *midRow = new QHBoxLayout();

    auto *cfgBox = new QGroupBox("Config (EEPROM Mirror)", root);
    auto *cfgGrid = new QGridLayout(cfgBox);

    thHigh_ = new QSpinBox(cfgBox);
    thLow_ = new QSpinBox(cfgBox);
    cooldown_ = new QSpinBox(cfgBox);
    defaultMode_ = new QComboBox(cfgBox);

    thHigh_->setRange(-550, 1250);
    thLow_->setRange(-550, 1250);
    cooldown_->setRange(0, 3600);
    defaultMode_->addItems({"AUTO", "MANUAL", "SAFE"});

    thHigh_->setValue(300);
    thLow_->setValue(290);
    cooldown_->setValue(30);

    auto *btnLoad = new QPushButton("Load Config", cfgBox);
    auto *btnSave = new QPushButton("Save Config", cfgBox);
    connect(btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadConfig);
    connect(btnSave, &QPushButton::clicked, this, &MainWindow::onSaveConfig);

    cfgGrid->addWidget(new QLabel("Temp High (x0.1C):"), 0, 0);
    cfgGrid->addWidget(thHigh_, 0, 1);
    cfgGrid->addWidget(new QLabel("Temp Low (x0.1C):"), 1, 0);
    cfgGrid->addWidget(thLow_, 1, 1);
    cfgGrid->addWidget(new QLabel("Cooldown (s):"), 2, 0);
    cfgGrid->addWidget(cooldown_, 2, 1);
    cfgGrid->addWidget(new QLabel("Default Mode:"), 3, 0);
    cfgGrid->addWidget(defaultMode_, 3, 1);
    cfgGrid->addWidget(btnLoad, 4, 0);
    cfgGrid->addWidget(btnSave, 4, 1);

    auto *scheduleBox = new QGroupBox("Schedule", root);
    auto *scheduleLayout = new QVBoxLayout(scheduleBox);

    scheduleTable_ = new QTableWidget(0, 4, scheduleBox);
    scheduleTable_->setHorizontalHeaderLabels({"Enable", "Time", "Action", "Param"});
    scheduleTable_->horizontalHeader()->setStretchLastSection(true);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    auto *scheduleBtnRow = new QHBoxLayout();
    auto *btnAddTask = new QPushButton("Add Task", scheduleBox);
    auto *btnDelTask = new QPushButton("Delete Last", scheduleBox);
    connect(btnAddTask, &QPushButton::clicked, this, &MainWindow::onAddSchedule);
    connect(btnDelTask, &QPushButton::clicked, [this]() {
        if (scheduleTable_->rowCount() > 0) {
            scheduleTable_->removeRow(scheduleTable_->rowCount() - 1);
            appendLog("Schedule: remove last row.");
        }
    });

    scheduleBtnRow->addWidget(btnAddTask);
    scheduleBtnRow->addWidget(btnDelTask);
    scheduleBtnRow->addStretch();

    scheduleLayout->addWidget(scheduleTable_);
    scheduleLayout->addLayout(scheduleBtnRow);

    midRow->addWidget(cfgBox, 2);
    midRow->addWidget(scheduleBox, 3);

    auto *logBox = new QGroupBox("Log", root);
    auto *logLayout = new QVBoxLayout(logBox);
    logView_ = new QTextEdit(logBox);
    logView_->setReadOnly(true);
    auto *btnClearLog = new QPushButton("Clear Log", logBox);
    connect(btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLog);

    logLayout->addWidget(logView_);
    logLayout->addWidget(btnClearLog);

    mainLayout->addWidget(serialBox);
    mainLayout->addLayout(topRow);
    mainLayout->addLayout(midRow);
    mainLayout->addWidget(logBox, 2);

    setCentralWidget(root);
    statusBar()->showMessage("Direct TCP mode: connect and watch RX log.");
    onLinkTypeChanged();
}

void MainWindow::appendLog(const QString &line)
{
    if (!logView_)
    {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    logView_->append(QString("[%1] %2").arg(ts, line));
}

void MainWindow::sendCommand(const QString &cmd)
{
    QStringList parts;
    QString head;
    QByteArray payload;

    if (cmd.isEmpty())
    {
        return;
    }

    const bool useTcp = (linkTypeBox_ && linkTypeBox_->currentText() == "TCP");

    if (useTcp)
    {
        if (!tcp_ || tcp_->state() != QAbstractSocket::ConnectedState)
        {
            appendLog(QString("TX skipped (tcp closed): %1").arg(cmd));
            return;
        }

        parts = cmd.trimmed().toUpper().split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty())
        {
            return;
        }

        head = parts.first();
        if (head == "PING")
        {
            sendPingRequest();
            return;
        }
        if (head == "MODE" && parts.size() >= 2)
        {
            unsigned char mode = 0x00;
            if (parts.at(1) == "AUTO") mode = 0;
            else if (parts.at(1) == "MANUAL") mode = 1;
            else if (parts.at(1) == "SAFE") mode = 2;
            else
            {
                appendLog(QString("TX skipped (bad MODE): %1").arg(cmd));
                return;
            }
            payload.append(static_cast<char>(mode));
            sendProtocolFrame(kCmdModeSetReq, nextSeq_++, payload);
            return;
        }
        if (head == "MOVE" && parts.size() >= 2)
        {
            bool ok = false;
            QString part = parts.at(1); // 自动创建一个可修改的副本
            int pct = part.remove('%').toInt(&ok);
            if (!ok || pct < 0 || pct > 100)
            {
                appendLog(QString("TX skipped (bad MOVE): %1").arg(cmd));
                return;
            }
            payload.append(static_cast<char>(pct));
            sendProtocolFrame(kCmdMoveToReq, nextSeq_++, payload);
            return;
        }
        if (head == "STOP")
        {
            sendProtocolFrame(kCmdStopReq, nextSeq_++, payload);
            return;
        }
        if (head.startsWith("BEEP"))
        {
            int count = 1;
            bool ok = false;
            if (head.size() > 4)
            {
                count = head.mid(4).toInt(&ok);
                if (!ok) count = 1;
            }
            else if (parts.size() >= 2)
            {
                count = parts.at(1).toInt(&ok);
                if (!ok) count = 1;
            }
            if (count < 1 || count > 5)
            {
                appendLog(QString("TX skipped (bad BEEP): %1").arg(cmd));
                return;
            }
            payload.append(static_cast<char>(count));
            sendProtocolFrame(kCmdBeepReq, nextSeq_++, payload);
            return;
        }
        if (head == "GETCFG")
        {
            sendProtocolFrame(kCmdCfgGetReq, nextSeq_++, payload);
            return;
        }
        if (head == "STATUS" || head == "STATUS?")
        {
            sendProtocolFrame(kCmdStatusGetReq, nextSeq_++, payload);
            return;
        }

        appendLog(QString("TX skipped (unknown proto cmd): %1").arg(cmd));
    }
    else
    {
        if (!serial_ || !serial_->isOpen() || !serial_->isWritable() || serial_->error() != QSerialPort::NoError)
        {
            appendLog(QString("TX skipped (serial not ready): %1").arg(cmd));
            return;
        }

        QByteArray data = cmd.toUtf8();
        if (!data.endsWith('\n'))
        {
            data.append('\n');
        }

        const qint64 written = serial_->write(data);
        if (written < 0)
        {
            appendLog(QString("TX[UART] failed: %1").arg(serial_->errorString()));
            return;
        }
        serial_->flush();
        appendLog(QString("TX[UART] -> %1").arg(cmd));
    }
}

void MainWindow::onRefreshPorts()
{
    QString manualPort;

    if (linkTypeBox_ && linkTypeBox_->currentText() == "TCP")
    {
        appendLog("Refresh ignored in TCP mode.");
        return;
    }

    manualPort = portBox_->currentText().trimmed();
    portBox_->clear();

    /* 保留常用虚拟/USB 串口项，便于 Linux 下手工选择 */
    portBox_->addItems({"/tmp/ttyV0", "/tmp/ttyUSB0", "/tmp/ttyACM0"});

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports)
    {
        portBox_->addItem(info.portName());
        if (!info.systemLocation().isEmpty())
        {
            portBox_->addItem(info.systemLocation());
        }
    }

    if (!manualPort.isEmpty())
    {
        if (portBox_->findText(manualPort) < 0)
        {
            portBox_->addItem(manualPort);
        }
        portBox_->setCurrentText(manualPort);
    }

    appendLog(QString("Port list refreshed: %1 found.").arg(ports.size()));
}

void MainWindow::onTogglePort()
{
    const bool useTcp = (linkTypeBox_ && linkTypeBox_->currentText() == "TCP");
    if (useTcp)
    {
        if (!tcp_)
        {
            return;
        }
        if (tcp_->state() == QAbstractSocket::ConnectedState ||
            tcp_->state() == QAbstractSocket::ConnectingState)
        {
            tcp_->disconnectFromHost();
            return;
        }
        const QString host = tcpHostEdit_ ? tcpHostEdit_->text().trimmed() : QString();
        const quint16 port = static_cast<quint16>(tcpPortSpin_ ? tcpPortSpin_->value() : 8888);
        if (host.isEmpty())
        {
            appendLog("TCP open failed: host is empty.");
            return;
        }
        appendLog(QString("TCP connecting: %1:%2").arg(host).arg(port));
        tcp_->connectToHost(host, port);
        return;
    }

    const QString portPath = "/tmp/ttyV0";

    if (!serial_ || !openBtn_ || !linkLabel_)
    {
        return;
    }

    if (serial_->isOpen())
    {
        serial_->close();
        openBtn_->setText("Open");
        linkLabel_->setText("Closed");
        appendLog("Serial closed.");
        return;
    }

    if (!QFileInfo::exists(portPath))
    {
        appendLog("Virtual serial /tmp/ttyV0 not found, start socat now.");
        startSocat();
        if (!QFileInfo::exists(portPath))
        {
            appendLog("Open failed: /tmp/ttyV0 is not ready.");
            return;
        }
    }

    QFileInfo info(portPath);
    if (!info.isReadable() || !info.isWritable())
    {
        appendLog("Open failed: no permission on /tmp/ttyV0.");
        return;
    }

    serial_->setPortName(portPath);
    serial_->setBaudRate(QSerialPort::Baud4800);
    serial_->setDataBits(QSerialPort::Data8);
    serial_->setParity(QSerialPort::NoParity);
    serial_->setStopBits(QSerialPort::OneStop);
    serial_->setFlowControl(QSerialPort::NoFlowControl);

    if (serial_->open(QIODevice::ReadWrite))
    {
        openBtn_->setText("Close");
        linkLabel_->setText("Opened");
        appendLog(QString("Serial opened: %1 @ %2")
                  .arg(serial_->portName())
                  .arg(serial_->baudRate()));
        appendLog("Serial is ready.");
    }
    else
    {
        appendLog(QString("Open failed: %1 (port=%2, exists=%3)")
                  .arg(serial_->errorString())
                  .arg(portPath)
                  .arg(QFileInfo::exists(portPath) ? "yes" : "no"));
    }
}

void MainWindow::startSocat()
{
    if (!tcpHostEdit_ || !tcpPortSpin_)
    {
        appendLog("socat start skipped: ui controls not ready.");
        return;
    }

    killStaleSocatFromPidFile();

    if (socatProcess_ && socatProcess_->state() != QProcess::NotRunning)
    {
        return;
    }

    if (!socatProcess_)
    {
        socatProcess_ = new QProcess(this);
        socatProcess_->setProcessChannelMode(QProcess::SeparateChannels);

        connect(socatProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            appendLog(QString("socat error: %1").arg(socatProcess_->errorString()));
        });
        connect(socatProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus status) {
            clearSocatPid();
            appendLog(QString("socat exited: code=%1 status=%2")
                      .arg(code)
                      .arg(status == QProcess::NormalExit ? "normal" : "crash"));
        });
        connect(socatProcess_, &QProcess::readyReadStandardError, this, [this]() {
            const QString err = QString::fromLocal8Bit(socatProcess_->readAllStandardError()).trimmed();
            if (!err.isEmpty())
            {
                appendLog(QString("socat: %1").arg(err));
            }
        });
    }

    const QString host = tcpHostEdit_ ? tcpHostEdit_->text().trimmed() : QString();
    const int port = tcpPortSpin_ ? tcpPortSpin_->value() : 8888;
    if (host.isEmpty())
    {
        appendLog("socat start skipped: tcp host is empty.");
        return;
    }

    const QString ptyArg = "pty,link=/tmp/ttyV0,raw,echo=0";
    const QString tcpArg = QString("tcp:%1:%2,forever,interval=1").arg(host).arg(port);
    socatProcess_->start("socat", QStringList() << ptyArg << tcpArg);

    if (!socatProcess_->waitForStarted(2000))
    {
        appendLog(QString("Failed to start socat: %1").arg(socatProcess_->errorString()));
        return;
    }

    QProcess::execute("chmod", QStringList() << "666" << "/tmp/ttyV0");
    saveSocatPid(static_cast<qint64>(socatProcess_->processId()));
    appendLog(QString("socat started: /tmp/ttyV0 <-> %1:%2").arg(host).arg(port));
}

void MainWindow::stopSocat()
{
    if (socatProcess_)
    {
        socatProcess_->kill();
        socatProcess_->waitForFinished(500);
        delete socatProcess_;
        socatProcess_ = nullptr;
    }

    QProcess::execute("pkill", QStringList() << "-9" << "-f" << "socat pty,link=/tmp/ttyV0");
    QFile::remove("/tmp/ttyV0");
    clearSocatPid();
}

void MainWindow::tryAutoOpenVirtualSerial()
{
    if (!serial_ || serial_->isOpen())
    {
        return;
    }

    if (!QFileInfo::exists("/tmp/ttyV0"))
    {
        appendLog("Auto-open skipped: /tmp/ttyV0 not ready.");
        return;
    }

    onTogglePort();
}

void MainWindow::appendRxBytes(const QByteArray &rx, const QString &tag)
{
    const QString hex = rx.toHex(' ').toUpper();
    if (logView_)
    {
        appendLog(QString("RX[%1] HEX: %2").arg(tag, hex));
    }
}

void MainWindow::sendProtocolFrame(unsigned char cmd, unsigned char seq, const QByteArray &payload)
{
    QByteArray frame;
    const unsigned char len = static_cast<unsigned char>(payload.size());
    const unsigned char chk = protocolChecksum(cmd, seq, payload);

    if (!tcp_ || tcp_->state() != QAbstractSocket::ConnectedState)
    {
        appendLog(QString("TX frame skipped (tcp closed), cmd=0x%1")
                  .arg(static_cast<unsigned int>(cmd), 2, 16, QLatin1Char('0')).toUpper());
        return;
    }

    frame.reserve(7 + payload.size());
    frame.append(static_cast<char>(0x55));
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(kProtoVer));
    frame.append(static_cast<char>(cmd));
    frame.append(static_cast<char>(seq));
    frame.append(static_cast<char>(len));
    frame.append(payload);
    frame.append(static_cast<char>(chk));

    tcp_->write(frame);
    appendLog(QString("TX FRAME cmd=0x%1 seq=%2 len=%3 hex=%4")
              .arg(static_cast<unsigned int>(cmd), 2, 16, QLatin1Char('0')).toUpper()
              .arg(static_cast<unsigned int>(seq))
              .arg(payload.size())
              .arg(frame.toHex(' ').toUpper().constData()));
}

void MainWindow::sendPingRequest()
{
    QByteArray payload;
    sendProtocolFrame(kCmdPingReq, nextSeq_++, payload);
}

void MainWindow::processTcpRxBuffer()
{
    int sof = 0;
    int frameLen = 0;
    unsigned char ver = 0;
    unsigned char cmd = 0;
    unsigned char seq = 0;
    unsigned char len = 0;
    unsigned char chk = 0;
    QByteArray payload;
    QByteArray frame;

    while (true)
    {
        sof = tcpRxBuffer_.indexOf(QByteArray::fromHex("55AA"));
        if (sof < 0)
        {
            tcpRxBuffer_.clear();
            return;
        }
        if (sof > 0)
        {
            tcpRxBuffer_.remove(0, sof);
        }
        if (tcpRxBuffer_.size() < 7)
        {
            return;
        }

        ver = static_cast<unsigned char>(tcpRxBuffer_.at(2));
        cmd = static_cast<unsigned char>(tcpRxBuffer_.at(3));
        seq = static_cast<unsigned char>(tcpRxBuffer_.at(4));
        len = static_cast<unsigned char>(tcpRxBuffer_.at(5));
        if (ver != kProtoVer || len > 64)
        {
            tcpRxBuffer_.remove(0, 1);
            continue;
        }

        frameLen = 7 + static_cast<int>(len);
        if (tcpRxBuffer_.size() < frameLen)
        {
            return;
        }

        payload = tcpRxBuffer_.mid(6, len);
        chk = static_cast<unsigned char>(tcpRxBuffer_.at(6 + len));
        if (protocolChecksum(cmd, seq, payload) != chk)
        {
            frame = tcpRxBuffer_.left(frameLen);
            appendLog(QString("RX FRAME drop: checksum mismatch hex=%1")
                      .arg(frame.toHex(' ').toUpper().constData()));
            tcpRxBuffer_.remove(0, 1);
            continue;
        }

        frame = tcpRxBuffer_.left(frameLen);
        appendLog(QString("RX FRAME cmd=0x%1 seq=%2 len=%3 hex=%4")
                  .arg(static_cast<unsigned int>(cmd), 2, 16, QLatin1Char('0')).toUpper()
                  .arg(static_cast<unsigned int>(seq))
                  .arg(static_cast<unsigned int>(len))
                  .arg(frame.toHex(' ').toUpper().constData()));
        handleProtocolFrame(cmd, seq, payload);
        tcpRxBuffer_.remove(0, frameLen);
    }
}

void MainWindow::handleProtocolFrame(unsigned char cmd, unsigned char seq, const QByteArray &payload)
{
    if (cmd == kCmdAck || cmd == kCmdNack)
    {
        const unsigned int code = payload.isEmpty() ? 0xFFu : static_cast<unsigned char>(payload.at(0));
        appendLog(QString("RX %1 seq=%2 code=%3")
                  .arg(cmd == kCmdAck ? "ACK" : "NACK")
                  .arg(static_cast<unsigned int>(seq))
                  .arg(code));
        return;
    }

    if ((cmd == kCmdStatusRsp || cmd == kCmdStatusEvt) && payload.size() >= 5)
    {
        const unsigned int mode = static_cast<unsigned char>(payload.at(0));
        const int temp10 = static_cast<unsigned char>(payload.at(1)) |
                           (static_cast<int>(static_cast<signed char>(payload.at(2))) << 8);
        const unsigned int pos = static_cast<unsigned char>(payload.at(3));
        const unsigned int err = static_cast<unsigned char>(payload.at(4));
        appendLog(QString("RX STATUS kind=0x%1 mode=%2 temp=%3 pos=%4 err=%5")
                  .arg(static_cast<unsigned int>(cmd), 2, 16, QLatin1Char('0')).toUpper()
                  .arg(mode)
                  .arg(QString::number(temp10 / 10.0, 'f', 1))
                  .arg(pos)
                  .arg(err));
        return;
    }

    appendLog(QString("RX FRAME unhandled cmd=0x%1 seq=%2 payload=%3")
              .arg(static_cast<unsigned int>(cmd), 2, 16, QLatin1Char('0')).toUpper()
              .arg(static_cast<unsigned int>(seq))
              .arg(payload.toHex(' ').toUpper().constData()));
}

void MainWindow::onModeChanged()
{
    appendLog(QString("Mode changed -> %1").arg(modeBox_->currentText()));
    sendCommand(QString("MODE %1").arg(modeBox_->currentText()));
}

void MainWindow::onActionClicked()
{
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) {
        return;
    }

    const QString cmd = btn->text();
    appendLog(QString("Action request: %1").arg(cmd));

    if (cmd.startsWith("MOVE")) {
        pos_ = cmd.mid(5).remove('%').toInt();
        actuatorLabel_->setText(QString("%1 %").arg(pos_));
        sendCommand(QString("MOVE %1").arg(pos_));
    } else if (cmd == "STOP") {
        errorLabel_->setText("MANUAL STOP");
        sendCommand("STOP");
    } else if (cmd == "BEEP x1") {
        sendCommand("BEEP1");
    } else if (cmd == "BEEP x3") {
        sendCommand("BEEP3");
    } else if (cmd == "CLEAR ERR") {
        errorLabel_->setText("NONE");
        sendCommand("CLEAR");
    }
}

void MainWindow::onSaveConfig()
{
    appendLog(QString("Save cfg: TH=%1 TL=%2 CD=%3 MODE=%4")
              .arg(thHigh_->value())
              .arg(thLow_->value())
              .arg(cooldown_->value())
              .arg(defaultMode_->currentText()));

    sendCommand(QString("SET TH %1").arg(thHigh_->value()));
    sendCommand(QString("SET TL %1").arg(thLow_->value()));
    sendCommand(QString("SET CD %1").arg(cooldown_->value()));
}

void MainWindow::onLoadConfig()
{
    appendLog("Load cfg clicked.");
    sendCommand("GETCFG");
}

void MainWindow::onAddSchedule()
{
    const int row = scheduleTable_->rowCount();
    scheduleTable_->insertRow(row);
    scheduleTable_->setItem(row, 0, new QTableWidgetItem("1"));
    scheduleTable_->setItem(row, 1, new QTableWidgetItem("07:30"));
    scheduleTable_->setItem(row, 2, new QTableWidgetItem("MOVE_TO"));
    scheduleTable_->setItem(row, 3, new QTableWidgetItem("80"));
    appendLog("Schedule: add demo task 07:30 MOVE_TO 80.");
}

void MainWindow::onClearLog()
{
    logView_->clear();
    appendLog("Log cleared.");
}

void MainWindow::onMockTick()
{
    clockLabel_->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));

    temp10_ += QRandomGenerator::global()->bounded(3) - 1;
    if (temp10_ < 260) temp10_ = 260;
    if (temp10_ > 340) temp10_ = 340;
    tempLabel_->setText(QString::number(temp10_ / 10.0, 'f', 1) + " C");

    if (temp10_ > thHigh_->value()) {
        errorLabel_->setText("TEMP HIGH");
    }
}
void MainWindow::onSendLine()
{
    sendCommand(txEdit_->text());
    txEdit_->clear();
}

void MainWindow::onSerialReadyRead()
{
    if (!serial_)
    {
        return;
    }

    const QByteArray rx = serial_->readAll();
    if (!rx.isEmpty())
    {
        appendRxBytes(rx, "UART");
    }
}

void MainWindow::onTcpReadyRead()
{
    if (!tcp_)
    {
        return;
    }

    tcpRxBuffer_.append(tcp_->readAll());
    processTcpRxBuffer();
}

void MainWindow::onTcpConnected()
{
    openBtn_->setText("Close");
    linkLabel_->setText("TCP Connected");
    appendLog(QString("TCP connected: %1:%2")
              .arg(tcp_->peerAddress().toString())
              .arg(tcp_->peerPort()));
    sendPingRequest();
}

void MainWindow::onTcpDisconnected()
{
    tcpRxBuffer_.clear();
    openBtn_->setText("Open");
    linkLabel_->setText("Closed");
    appendLog("TCP disconnected.");
}

void MainWindow::onTcpError()
{
    if (!tcp_)
    {
        return;
    }
    openBtn_->setText("Open");
    linkLabel_->setText("Closed");
    appendLog(QString("TCP error: %1").arg(tcp_->errorString()));
}

void MainWindow::onLinkTypeChanged()
{
    if (!linkTypeBox_ || !portBox_ || !baudBox_ || !tcpHostEdit_ || !tcpPortSpin_ || !linkLabel_)
    {
        return;
    }

    const bool useTcp = (linkTypeBox_ && linkTypeBox_->currentText() == "TCP");

    portBox_->setEnabled(!useTcp);
    baudBox_->setEnabled(!useTcp);
    tcpHostEdit_->setEnabled(useTcp);
    tcpPortSpin_->setEnabled(useTcp);

    if (useTcp)
    {
        if (serial_ && serial_->isOpen())
        {
            serial_->close();
        }
        linkLabel_->setText((tcp_ && tcp_->state() == QAbstractSocket::ConnectedState) ? "TCP Connected" : "Closed");
    }
    else
    {
        if (tcp_ && tcp_->state() != QAbstractSocket::UnconnectedState)
        {
            tcp_->disconnectFromHost();
        }
        linkLabel_->setText(serial_ && serial_->isOpen() ? "Opened" : "Closed");
    }
}
