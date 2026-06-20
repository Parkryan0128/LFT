#pragma once

#include <QObject>
#include <QString>

class RecvPage;

class RecvWorker : public QObject {
    Q_OBJECT

public:
    explicit RecvWorker(RecvPage* page);

public slots:
    void run(QString out_dir, QString device_name, quint16 port);

signals:
    void status(const QString& text);
    void progress(int percent, const QString& detail);
    void finished(bool ok, const QString& message);

private:
    RecvPage* page_ = nullptr;
};
