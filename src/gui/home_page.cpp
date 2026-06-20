#include "gui/home_page.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(16);

    auto* title = new QLabel("LFT", this);
    title->setAlignment(Qt::AlignCenter);
    QFont title_font = title->font();
    title_font.setPointSize(24);
    title_font.setBold(true);
    title->setFont(title_font);

    auto* subtitle = new QLabel("LAN File Transfer", this);
    subtitle->setAlignment(Qt::AlignCenter);

    receive_btn_ = new QPushButton("Receive", this);
    send_btn_ = new QPushButton("Send", this);
    receive_btn_->setMinimumHeight(48);
    send_btn_->setMinimumHeight(48);

    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(24);
    layout->addWidget(receive_btn_);
    layout->addWidget(send_btn_);
    layout->addStretch();

    connect(receive_btn_, &QPushButton::clicked, this, &HomePage::receiveRequested);
    connect(send_btn_, &QPushButton::clicked, this, &HomePage::sendRequested);
}
