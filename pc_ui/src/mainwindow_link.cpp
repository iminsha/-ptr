#include "mainwindow.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QStringList>
#include <QTextEdit>
#include <QTcpSocket>
#include <QTextStream>

#include <signal.h>

namespace {
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
            QString part = parts.at(1);
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

        if (modeBox_)
        {
            if (mode == 0) modeBox_->setCurrentText("AUTO");
            else if (mode == 1) modeBox_->setCurrentText("MANUAL");
            else if (mode == 2) modeBox_->setCurrentText("SAFE");
        }
        temp10_ = temp10;
        if (tempLabel_) tempLabel_->setText(QString::number(temp10_ / 10.0, 'f', 1) + " C");
        if (actuatorLabel_) actuatorLabel_->setText(QString("%1 %").arg(pos));
        if (errorLabel_) errorLabel_->setText(err == 0 ? "NONE" : QString("ERR_%1").arg(err));

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
