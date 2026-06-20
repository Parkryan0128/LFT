#pragma once

#include <QWidget>

class QPushButton;

class HomePage : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);

signals:
    void receiveRequested();
    void sendRequested();

private:
    QPushButton* receive_btn_ = nullptr;
    QPushButton* send_btn_ = nullptr;
};
