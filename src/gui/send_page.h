#pragma once

#include <QWidget>
#include <QString>

#include <optional>

class QListWidget;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;

struct PeerRow {
    QString name;
    QString host;
    quint16 port = 0;
};
Q_DECLARE_METATYPE(PeerRow)
Q_DECLARE_METATYPE(QList<PeerRow>)

class SendPage : public QWidget {
    Q_OBJECT

public:
    explicit SendPage(QWidget* parent = nullptr);
    ~SendPage() override;

    void reset();
    void startDiscovery();

signals:
    void backRequested();

private slots:
    void refreshPeers();
    void browseFile();
    void sendFile();
    void onBrowseStatus(const QString& text);
    void onBrowseFinished(const QList<PeerRow>& peers, const QString& error);
    void onSendStatus(const QString& text);
    void onSendProgress(int percent, const QString& detail);
    void onSendFinished(bool ok, const QString& message);

private:
    void setBusy(bool busy);
    std::optional<PeerRow> selectedPeer() const;

    QListWidget* peer_list_ = nullptr;
    QLineEdit* file_edit_ = nullptr;
    QLineEdit* manual_host_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    QPushButton* back_btn_ = nullptr;

    QThread* browse_thread_ = nullptr;
    QThread* send_thread_ = nullptr;
};
