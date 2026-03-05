#include "mainwindow.h"

#include <QAbstractSocket>
#include <QComboBox>
#include <QLineEdit>
#include <QSerialPort>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr bool kDirectTcpMode = true;
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
