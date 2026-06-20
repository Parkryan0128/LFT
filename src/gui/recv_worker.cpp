#include "gui/recv_worker.h"

#include "gui/format.h"
#include "gui/gui_constants.h"
#include "gui/recv_page.h"
#include "net/mdns.h"
#include "transfer/quic_server.h"

#include <QMetaObject>

#include <unistd.h>

#include <filesystem>
#include <memory>

namespace {

QString defaultHostName() {
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0) {
        return "this device";
    }
    return QString::fromLocal8Bit(buf);
}

}  // namespace

RecvWorker::RecvWorker(RecvPage* page) : page_(page) {}

void RecvWorker::run(QString out_dir, QString device_name, quint16 port) {
    std::error_code ec;
    const std::string out = out_dir.toStdString();
    std::filesystem::create_directories(out, ec);
    if (!std::filesystem::is_directory(out, ec)) {
        emit finished(false, QString("Save folder is not valid: %1").arg(out_dir));
        return;
    }

    auto server = std::make_unique<lft::QuicServer>(port);
    if (!server->start("0.0.0.0")) {
        emit finished(false, QString("Failed to listen on port %1").arg(port));
        return;
    }

    auto advertiser = std::make_unique<lft::MdnsAdvertiser>();
    const std::string name = device_name.trimmed().toStdString();
    if (advertiser->start(name, port)) {
        const QString shown = device_name.trimmed().isEmpty() ? defaultHostName() : device_name.trimmed();
        emit status(QString("Waiting for sender… discoverable as \"%1\"").arg(shown));
    } else {
        emit status("Waiting for sender… (LAN discovery unavailable)");
    }

    auto offer = [this](const lft::FileTransferHeader& header) -> bool {
        bool accepted = false;
        QMetaObject::invokeMethod(
            page_,
            "askAcceptOffer",
            Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(bool, accepted),
            Q_ARG(QString, QString::fromStdString(header.name)),
            Q_ARG(quint64, static_cast<quint64>(header.size)));
        return accepted;
    };

    RecvPage* page = page_;
    auto progress = [page](uint64_t done, uint64_t total) {
        const int pct = total == 0 ? 100 : static_cast<int>((done * 100) / total);
        const QString detail =
            QString("%1 / %2").arg(lft::gui::formatBytes(done), lft::gui::formatBytes(total));
        QMetaObject::invokeMethod(page,
                                  "onProgress",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, pct),
                                  Q_ARG(QString, detail));
    };

    const bool ok =
        server->receive_file(out, lft::gui::kRecvTimeoutMs, nullptr, offer, progress);
    server->stop();

    const lft::FileReceiveResult& result = server->last_file_result();
    if (!ok) {
        if (result.rejected) {
            emit finished(true, "Transfer declined.");
            return;
        }
        QString msg = "Receive failed.";
        if (!result.error.empty()) {
            msg += " " + QString::fromStdString(result.error);
        }
        emit finished(false, msg);
        return;
    }

    emit finished(true,
                  QString("Saved \"%1\" (%2, SHA-256 verified)")
                      .arg(QString::fromStdString(result.path))
                      .arg(lft::gui::formatBytes(result.bytes_received)));
}
