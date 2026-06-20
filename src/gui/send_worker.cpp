#include "gui/send_worker.h"

#include "gui/format.h"
#include "gui/gui_constants.h"
#include "net/mdns.h"
#include "transfer/quic_client.h"

#include <QMetaObject>

#include <filesystem>

void BrowseWorker::run() {
    emit status("Searching for receivers on the LAN…");

    lft::MdnsBrowser browser;
    const std::vector<lft::PeerInfo> peers = browser.browse(lft::gui::kDiscoveryTimeoutMs);

    QList<PeerRow> rows;
    for (const lft::PeerInfo& peer : peers) {
        rows.push_back(PeerRow{
            .name = QString::fromStdString(peer.name),
            .host = QString::fromStdString(peer.host),
            .port = peer.port,
        });
    }

    if (rows.isEmpty()) {
        emit finished(rows, "No receivers found. Start receive mode on the other device.");
        return;
    }

    emit finished(rows, {});
}

void SendWorker::run(QString file_path, PeerRow peer) {
    runManual(std::move(file_path), peer.host, peer.port);
}

void SendWorker::runManual(QString file_path, QString host, quint16 port) {
    std::error_code ec;
    const std::string path = file_path.toStdString();
    if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec)) {
        emit finished(false, "File not found: " + file_path);
        return;
    }

    emit status(QString("Connecting to %1:%2…").arg(host).arg(port));

    lft::QuicClient client;
    if (!client.connect(host.toStdString(), port)) {
        emit finished(false, QString("Could not connect to %1:%2").arg(host).arg(port));
        return;
    }

    SendWorker* self = this;
    auto progress = [self](uint64_t done, uint64_t total) {
        const int pct = total == 0 ? 100 : static_cast<int>((done * 100) / total);
        const QString detail =
            QString("%1 / %2").arg(lft::gui::formatBytes(done), lft::gui::formatBytes(total));
        QMetaObject::invokeMethod(self,
                                  "reportProgress",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, pct),
                                  Q_ARG(QString, detail));
    };

    emit status("Sending file…");
    const bool ok = client.send_file(path, lft::gui::kSendTimeoutMs, progress);
    const bool rejected = client.was_rejected();
    client.disconnect();

    if (!ok) {
        if (rejected) {
            emit finished(false, "Transfer rejected by receiver.");
        } else {
            emit finished(false, "Transfer failed.");
        }
        return;
    }

    emit finished(true, QString("Sent \"%1\" successfully.").arg(file_path));
}

void SendWorker::reportProgress(int percent, const QString& detail) {
    emit progress(percent, detail);
}
