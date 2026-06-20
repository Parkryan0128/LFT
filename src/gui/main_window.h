#pragma once

#include <QMainWindow>

class QStackedWidget;
class HomePage;
class RecvPage;
class SendPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void showHome();
    void showReceive();
    void showSend();

private:
    QStackedWidget* stack_ = nullptr;
    HomePage* home_page_ = nullptr;
    RecvPage* recv_page_ = nullptr;
    SendPage* send_page_ = nullptr;
};
