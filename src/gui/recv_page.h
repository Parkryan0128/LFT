#pragma once

#include <QWidget>

class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;

class RecvPage : public QWidget {
    Q_OBJECT

public:
    explicit RecvPage(QWidget* parent = nullptr);
    ~RecvPage() override;

    void reset();

signals:
    void backRequested();

public slots:
    bool askAcceptOffer(const QString& fileName, quint64 fileSize);

private slots:
    void browseOutDir();
    void startReceive();
    void onStatus(const QString& text);
    void onProgress(int percent, const QString& detail);
    void onFinished(bool ok, const QString& message);

private:
    void setBusy(bool busy);

    QLineEdit* out_dir_edit_ = nullptr;
    QLineEdit* name_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* start_btn_ = nullptr;
    QPushButton* back_btn_ = nullptr;

    QThread* worker_thread_ = nullptr;
};
