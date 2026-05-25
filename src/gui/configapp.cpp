#include "mainwindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("sc2gyrodsu-config");
    app.setApplicationDisplayName("SteamControllerGyroDSU Configuration");
    app.setApplicationVersion("1.0");

    sc2::MainWindow win;
    win.show();

    return app.exec();
}
