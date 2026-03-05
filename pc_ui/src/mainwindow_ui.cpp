#include "mainwindow.h"

#include <QAbstractSocket>
#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSerialPort>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTcpSocket>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
constexpr bool kDirectTcpMode = true;
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
        if (scheduleTable_->rowCount() > 0)
        {
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

void MainWindow::onModeChanged()
{
    appendLog(QString("Mode changed -> %1").arg(modeBox_->currentText()));
    sendCommand(QString("MODE %1").arg(modeBox_->currentText()));
}

void MainWindow::onActionClicked()
{
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn)
    {
        return;
    }

    const QString cmd = btn->text();
    appendLog(QString("Action request: %1").arg(cmd));

    if (cmd.startsWith("MOVE"))
    {
        pos_ = cmd.mid(5).remove('%').toInt();
        actuatorLabel_->setText(QString("%1 %").arg(pos_));
        sendCommand(QString("MOVE %1").arg(pos_));
    }
    else if (cmd == "STOP")
    {
        errorLabel_->setText("MANUAL STOP");
        sendCommand("STOP");
    }
    else if (cmd == "BEEP x1")
    {
        sendCommand("BEEP1");
    }
    else if (cmd == "BEEP x3")
    {
        sendCommand("BEEP3");
    }
    else if (cmd == "CLEAR ERR")
    {
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

    if (tcp_ && tcp_->state() == QAbstractSocket::ConnectedState)
    {
        return;
    }

    temp10_ += QRandomGenerator::global()->bounded(3) - 1;
    if (temp10_ < 260)
    {
        temp10_ = 260;
    }
    if (temp10_ > 340)
    {
        temp10_ = 340;
    }
    tempLabel_->setText(QString::number(temp10_ / 10.0, 'f', 1) + " C");

    if (temp10_ > thHigh_->value())
    {
        errorLabel_->setText("TEMP HIGH");
    }
}

void MainWindow::onSendLine()
{
    sendCommand(txEdit_->text());
    txEdit_->clear();
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
