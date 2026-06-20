#include "gui/send_page.h"

#include "gui/gui_constants.h"
#include "gui/send_worker.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVariant>
#include <QVBoxLayout>

SendPage::SendPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    peer_list_ = new QListWidget(this);
    peer_list_->setMinimumHeight(120);

    refresh_btn_ = new QPushButton("Refresh device list", this);

    file_edit_ = new QLineEdit(this);
    file_edit_->setReadOnly(true);
    auto* file_browse = new QPushButton("Choose file…", this);
    auto* file_row = new QHBoxLayout();
    file_row->addWidget(file_edit_, 1);
    file_row->addWidget(file_browse);

    manual_host_edit_ = new QLineEdit(this);
    manual_host_edit_->setPlaceholderText("Manual IP fallback (optional, e.g. 192.168.1.42)");

    status_label_ = new QLabel("Searching for receivers on the LAN…", this);
    status_label_->setWordWrap(true);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    progress_bar_->setVisible(false);

    auto* btn_row = new QHBoxLayout();
    back_btn_ = new QPushButton("Back", this);
    send_btn_ = new QPushButton("Send", this);
    btn_row->addWidget(back_btn_);
    btn_row->addStretch();
    btn_row->addWidget(send_btn_);

    layout->addWidget(new QLabel("Receivers on LAN:", this));
    layout->addWidget(peer_list_);
    layout->addWidget(refresh_btn_);
    layout->addWidget(new QLabel("File to send:", this));
    layout->addLayout(file_row);
    layout->addWidget(new QLabel("Or connect by IP:", this));
    layout->addWidget(manual_host_edit_);
    layout->addWidget(status_label_);
    layout->addWidget(progress_bar_);
    layout->addLayout(btn_row);

    connect(refresh_btn_, &QPushButton::clicked, this, &SendPage::refreshPeers);
    connect(file_browse, &QPushButton::clicked, this, &SendPage::browseFile);
    connect(send_btn_, &QPushButton::clicked, this, &SendPage::sendFile);
    connect(back_btn_, &QPushButton::clicked, this, &SendPage::backRequested);
}

SendPage::~SendPage() {
    if (browse_thread_ != nullptr) {
        browse_thread_->quit();
        browse_thread_->wait();
    }
    if (send_thread_ != nullptr) {
        send_thread_->quit();
        send_thread_->wait();
    }
}

void SendPage::reset() {
    peer_list_->clear();
    progress_bar_->setVisible(false);
    progress_bar_->setValue(0);
    file_edit_->clear();
    manual_host_edit_->clear();
    status_label_->setText("Searching for receivers on the LAN…");
    refresh_btn_->setEnabled(false);
    send_btn_->setEnabled(true);
    back_btn_->setEnabled(true);
    peer_list_->setEnabled(true);
    manual_host_edit_->setEnabled(true);
}

void SendPage::startDiscovery() {
    refreshPeers();
}

void SendPage::refreshPeers() {
    if (browse_thread_ != nullptr || send_thread_ != nullptr) {
        return;
    }

    peer_list_->clear();
    status_label_->setText("Searching…");
    refresh_btn_->setEnabled(false);

    browse_thread_ = new QThread(this);
    auto* worker = new BrowseWorker();
    worker->moveToThread(browse_thread_);

    connect(browse_thread_, &QThread::started, worker, &BrowseWorker::run);
    connect(worker, &BrowseWorker::status, this, &SendPage::onBrowseStatus);
    connect(worker, &BrowseWorker::finished, this, &SendPage::onBrowseFinished);
    connect(worker, &BrowseWorker::finished, browse_thread_, &QThread::quit);
    connect(browse_thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(browse_thread_, &QThread::finished, this, [this]() {
        browse_thread_->deleteLater();
        browse_thread_ = nullptr;
        refresh_btn_->setEnabled(send_thread_ == nullptr);
    });

    browse_thread_->start();
}

void SendPage::browseFile() {
    const QString file =
        QFileDialog::getOpenFileName(this, "Choose file to send", {}, "All files (*)");
    if (!file.isEmpty()) {
        file_edit_->setText(file);
    }
}

void SendPage::sendFile() {
    if (file_edit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Send", "Choose a file first.");
        return;
    }
    if (send_thread_ != nullptr) {
        return;
    }

    const QString manual_host = manual_host_edit_->text().trimmed();
    const auto peer = selectedPeer();

    if (!manual_host.isEmpty() && peer.has_value()) {
        QMessageBox::warning(this, "Send", "Pick either a device from the list or enter a manual IP, not both.");
        return;
    }
    if (manual_host.isEmpty() && !peer.has_value()) {
        QMessageBox::warning(this, "Send", "Select a receiver or enter a manual IP.");
        return;
    }

    setBusy(true);
    progress_bar_->setVisible(true);
    progress_bar_->setValue(0);

    send_thread_ = new QThread(this);
    auto* worker = new SendWorker();
    worker->moveToThread(send_thread_);

    const QString file = file_edit_->text().trimmed();
    if (peer.has_value()) {
        const PeerRow selected = *peer;
        connect(send_thread_, &QThread::started, worker, [worker, file, selected]() {
            worker->run(file, selected);
        });
    } else {
        connect(send_thread_, &QThread::started, worker, [worker, file, manual_host]() {
            worker->runManual(file, manual_host, lft::gui::kDefaultPort);
        });
    }

    connect(worker, &SendWorker::status, this, &SendPage::onSendStatus);
    connect(worker, &SendWorker::progress, this, &SendPage::onSendProgress);
    connect(worker, &SendWorker::finished, this, &SendPage::onSendFinished);
    connect(worker, &SendWorker::finished, send_thread_, &QThread::quit);
    connect(send_thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(send_thread_, &QThread::finished, this, [this]() {
        send_thread_->deleteLater();
        send_thread_ = nullptr;
        setBusy(false);
    });

    send_thread_->start();
}

void SendPage::onBrowseStatus(const QString& text) {
    status_label_->setText(text);
}

void SendPage::onBrowseFinished(const QList<PeerRow>& peers, const QString& error) {
    peer_list_->clear();
    for (const PeerRow& peer : peers) {
        auto* item = new QListWidgetItem(
            QString("%1  (%2:%3)").arg(peer.name, peer.host).arg(peer.port));
        item->setData(Qt::UserRole, QVariant::fromValue(peer));
        peer_list_->addItem(item);
    }

    if (!error.isEmpty()) {
        status_label_->setText(error);
    } else {
        status_label_->setText(QString("Found %1 receiver(s). Select one and send.").arg(peers.size()));
    }
}

void SendPage::onSendStatus(const QString& text) {
    status_label_->setText(text);
}

void SendPage::onSendProgress(int percent, const QString& detail) {
    progress_bar_->setValue(percent);
    status_label_->setText(QString("Sending… %1% (%2)").arg(percent).arg(detail));
}

void SendPage::onSendFinished(bool ok, const QString& message) {
    progress_bar_->setValue(ok ? 100 : progress_bar_->value());
    status_label_->setText(message);

    if (ok) {
        QMessageBox::information(this, "Send", message);
    } else {
        QMessageBox::warning(this, "Send", message);
    }

    emit backRequested();
}

void SendPage::setBusy(bool busy) {
    const bool sending = send_thread_ != nullptr;
    refresh_btn_->setEnabled(!busy && !sending);
    send_btn_->setEnabled(!busy && !sending);
    back_btn_->setEnabled(!busy && !sending);
    peer_list_->setEnabled(!busy && !sending);
    manual_host_edit_->setEnabled(!busy && !sending);
}

std::optional<PeerRow> SendPage::selectedPeer() const {
    const QListWidgetItem* item = peer_list_->currentItem();
    if (item == nullptr) {
        return std::nullopt;
    }
    const QVariant data = item->data(Qt::UserRole);
    if (!data.isValid()) {
        return std::nullopt;
    }
    return data.value<PeerRow>();
}
