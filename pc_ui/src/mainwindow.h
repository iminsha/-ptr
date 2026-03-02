#pragma once

#include <QMainWindow>

class QLabel;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QTimer;
class QPushButton;
class QSerialPort;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

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

private:
    void buildUi();
    void appendLog(const QString &line);
    void sendCommand(const QString &cmd);

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
};
