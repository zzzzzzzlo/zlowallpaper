#include "app/Application.h"
#include "system/SingleInstanceGuard.h"
#include <QApplication>
#include <QIcon>
#include <QImageReader>
#include <QDir>
#include <QTimer>
#include <QWidget>
int main(int argc, char* argv[]) {
    QImageReader::setAllocationLimit(256);
    QApplication app(argc, argv);
    app.setOrganizationName("ZloWallpaper");
    app.setApplicationName("ZloWallpaper");
    app.setApplicationVersion("0.2.0");
    app.setWindowIcon(QIcon(":/icons/asterol.ico"));
    const bool smoke =
        app.arguments().contains("--smoke-existing") || app.arguments().contains("--smoke-recover");
    const bool smokeRecover = app.arguments().contains("--smoke-recover");
    const bool icontraSmoke = app.arguments().contains("--icontra-smoke");
    SingleInstanceGuard guard;
    if (!smoke && !icontraSmoke && !guard.acquire())
        return 0;
    Application application;
    application.start();
    if (app.arguments().contains("--ui-smoke")) {
        QTimer::singleShot(2500, &app, [&app] {
            const auto args = app.arguments();
            const int index = args.indexOf("--screenshot");
            if (index >= 0 && index + 1 < args.size())
                for (auto* widget : app.topLevelWidgets())
                    if (widget->inherits("QMainWindow")) widget->grab().save(args[index + 1]);
            app.quit();
        });
    }
    if (icontraSmoke) {
        application.startIcontraSmoke();
        QTimer::singleShot(10000, &app, &QCoreApplication::quit);
    } else if (smoke) {
        application.startSmokeWallpaper();
        if (smokeRecover)
            QTimer::singleShot(700, &application, &Application::recoverSmokeWallpaper);
        QTimer::singleShot(6000, &app, &QCoreApplication::quit);
    } else {
        QObject::connect(&guard, &SingleInstanceGuard::activationRequested, &app, [&] {
            for (auto* w : app.topLevelWidgets())
                if (w->inherits("QMainWindow")) {
                    w->showNormal();
                    w->raise();
                    w->activateWindow();
                }
        });
    }
    return app.exec();
}
