#include "gui/recv_page.h"

#include "gui/format.h"
#include "lft/constants.h"
#include "gui/recv_worker.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

RecvPage::RecvPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* out_row = new QHBoxLayout();
    out_dir_edit_ = new QLineEdit(this);
    out_dir_edit_->setReadOnly(true);
    auto* out_browse = new QPushButton("Choose folder…", this);
    out_row->addWidget(out_dir_edit_, 1);
    out_row->addWidget(out_browse);

    name_edit_ = new QLineEdit(this);
    name_edit_->setPlaceholderText("Device name (optional, defaults to hostname)");

    status_label_ = new QLabel("Pick a folder, then start receiving.", this);
    status_label_->setWordWrap(true);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->setVisible(false);

    auto* btn_row = new QHBoxLayout();
    back_btn_ = new QPushButton("Back", this);
    start_btn_ = new QPushButton("Start receiving", this);
    btn_row->addWidget(back_btn_);
    btn_row->addStretch();
    btn_row->addWidget(start_btn_);

    layout->addWidget(new QLabel("Save received files to:", this));
    layout->addLayout(out_row);
    layout->addWidget(new QLabel("Advertised name:", this));
    layout->addWidget(name_edit_);
    layout->addWidget(status_label_);
    layout->addWidget(progress_bar_);
    layout->addLayout(btn_row);

    const QString downloads =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!downloads.isEmpty()) {
        out_dir_edit_->setText(downloads);
    }

    connect(out_browse, &QPushButton::clicked, this, &RecvPage::browseOutDir);
    connect(start_btn_, &QPushButton::clicked, this, &RecvPage::startReceive);
    connect(back_btn_, &QPushButton::clicked, this, &RecvPage::backRequested);
}

RecvPage::~RecvPage() {
    if (worker_thread_ != nullptr) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
}

void RecvPage::reset() {
    progress_bar_->setVisible(false);
    progress_bar_->setValue(0);
    status_label_->setText("Pick a folder, then start receiving.");
    setBusy(false);
}

void RecvPage::browseOutDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Choose save folder",
                                                          out_dir_edit_->text());
    if (!dir.isEmpty()) {
        out_dir_edit_->setText(dir);
    }
}

void RecvPage::startReceive() {
    if (out_dir_edit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Receive", "Choose a save folder first.");
        return;
    }
    if (worker_thread_ != nullptr) {
        return;
    }

    setBusy(true);
    progress_bar_->setVisible(true);
    progress_bar_->setValue(0);
    status_label_->setText("Starting receiver…");

    worker_thread_ = new QThread(this);
    auto* worker = new RecvWorker(this);
    worker->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started, worker, [worker, this]() {
        worker->run(out_dir_edit_->text().trimmed(), name_edit_->text(),
                    lft::kDefaultPort);
    });
    connect(worker, &RecvWorker::status, this, &RecvPage::onStatus);
    connect(worker, &RecvWorker::progress, this, &RecvPage::onProgress);
    connect(worker, &RecvWorker::finished, this, &RecvPage::onFinished);
    connect(worker, &RecvWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, this, [this]() {
        worker_thread_->deleteLater();
        worker_thread_ = nullptr;
    });

    worker_thread_->start();
}

bool RecvPage::askAcceptOffer(const QString& fileName, quint64 fileSize) {
    const auto answer = QMessageBox::question(
        this,
        "Incoming file",
        QString("Accept \"%1\" (%2)?").arg(fileName, lft::gui::formatBytes(fileSize)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    return answer == QMessageBox::Yes;
}

void RecvPage::onStatus(const QString& text) {
    status_label_->setText(text);
}

void RecvPage::onProgress(int percent, const QString& detail) {
    progress_bar_->setValue(percent);
    status_label_->setText(QString("Receiving… %1% (%2)").arg(percent).arg(detail));
}

void RecvPage::onFinished(bool ok, const QString& message) {
    progress_bar_->setValue(ok ? 100 : progress_bar_->value());
    status_label_->setText(message);
    setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, "Receive", message);
        return;
    }
    if (message.contains("declined", Qt::CaseInsensitive)) {
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle("Receive");
    box.setIcon(QMessageBox::Information);
    box.setText("Transfer complete");
    box.setInformativeText(message);

    QPushButton* reveal_btn = nullptr;
#if defined(Q_OS_MAC)
    reveal_btn = box.addButton("Show in Finder", QMessageBox::ActionRole);
#endif
    box.addButton(QMessageBox::Ok);

    box.exec();

#if defined(Q_OS_MAC)
    if (reveal_btn != nullptr && box.clickedButton() == reveal_btn) {
        const QString path = message.section('\n', 1, 1).trimmed();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    }
#endif

    emit backRequested();
}

void RecvPage::setBusy(bool busy) {
    start_btn_->setEnabled(!busy);
    back_btn_->setEnabled(!busy);
    name_edit_->setEnabled(!busy);
}
