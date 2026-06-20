#pragma once

#include "gui/send_page.h"

#include <QObject>
#include <QString>

class BrowseWorker : public QObject {
    Q_OBJECT

public slots:
    void run();

signals:
    void status(const QString& text);
    void finished(const QList<PeerRow>& peers, const QString& error);
};

class SendWorker : public QObject {
    Q_OBJECT

public slots:
    void run(QString file_path, PeerRow peer);
    void runManual(QString file_path, QString host, quint16 port);
    void reportProgress(int percent, const QString& detail);

signals:
    void status(const QString& text);
    void progress(int percent, const QString& detail);
    void finished(bool ok, const QString& message);
};
