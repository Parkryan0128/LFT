#include "gui/main_window.h"

#include "gui/home_page.h"
#include "gui/recv_page.h"
#include "gui/send_page.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("LFT — LAN File Transfer");
    resize(480, 520);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 12);

    stack_ = new QStackedWidget(central);
    home_page_ = new HomePage(stack_);
    recv_page_ = new RecvPage(stack_);
    send_page_ = new SendPage(stack_);

    stack_->addWidget(home_page_);
    stack_->addWidget(recv_page_);
    stack_->addWidget(send_page_);

    layout->addWidget(stack_);
    setCentralWidget(central);

    connect(home_page_, &HomePage::receiveRequested, this, &MainWindow::showReceive);
    connect(home_page_, &HomePage::sendRequested, this, &MainWindow::showSend);
    connect(recv_page_, &RecvPage::backRequested, this, &MainWindow::showHome);
    connect(send_page_, &SendPage::backRequested, this, &MainWindow::showHome);
}

void MainWindow::showHome() {
    stack_->setCurrentWidget(home_page_);
}

void MainWindow::showReceive() {
    recv_page_->reset();
    stack_->setCurrentWidget(recv_page_);
}

void MainWindow::showSend() {
    send_page_->reset();
    stack_->setCurrentWidget(send_page_);
    send_page_->startDiscovery();
}
