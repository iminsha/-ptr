#pragma once

#include <QMainWindow>
#include <QByteArray>

class QLabel;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QTimer;
class QPushButton;
class QSerialPort;
class QTcpSocket;
class QProcess;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onModeChanged();
    void onActionClicked();
    void onSaveConfig();
    void onLoadConfig();
    void onAddSchedule();
    void onClearLog();
    void onMockTick();

    void onRefreshPorts();
    void onTogglePort();
    void onSendLine();
    void onSerialReadyRead();
    void onTcpReadyRead();
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpError();
    void onLinkTypeChanged();

private:
    void buildUi();
    void appendLog(const QString &line);
    void sendCommand(const QString &cmd);
    void appendRxBytes(const QByteArray &rx, const QString &tag);

    void sendProtocolFrame(unsigned char cmd, unsigned char seq, const QByteArray &payload);
    void sendPingRequest();
    void processTcpRxBuffer();
    void handleProtocolFrame(unsigned char cmd, unsigned char seq, const QByteArray &payload);

    void startSocat();
    void stopSocat();
    void tryAutoOpenVirtualSerial();

    QComboBox *modeBox_{};
    QLabel *clockLabel_{};
    QLabel *tempLabel_{};
    QLabel *actuatorLabel_{};
    QLabel *errorLabel_{};

    QSpinBox *thHigh_{};
    QSpinBox *thLow_{};
    QSpinBox *cooldown_{};
    QComboBox *defaultMode_{};

    QTableWidget *scheduleTable_{};
    QTextEdit *logView_{};

    QTimer *mockTimer_{};
    int temp10_ = 295;
    int pos_ = 30;

    QSerialPort *serial_{};
    QComboBox *portBox_{};
    QComboBox *baudBox_{};
    QPushButton *openBtn_{};
    QLineEdit *txEdit_{};
    QLabel *linkLabel_{};
    QComboBox *linkTypeBox_{};
    QLineEdit *tcpHostEdit_{};
    QSpinBox *tcpPortSpin_{};
    QTcpSocket *tcp_{};
    QByteArray tcpRxBuffer_{};
    unsigned char nextSeq_{1};

    QProcess *socatProcess_{};
    bool socatManagedMode_{true};
    bool autoOpenTried_{false};
};
