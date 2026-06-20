#include "gui/main_window.h"
#include "gui/send_page.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    qRegisterMetaType<PeerRow>("PeerRow");
    qRegisterMetaType<QList<PeerRow>>("QList<PeerRow>");

    QApplication app(argc, argv);
    app.setApplicationName("LFT");
    app.setOrganizationName("LFT");

    MainWindow window;
    window.show();

    return app.exec();
}
