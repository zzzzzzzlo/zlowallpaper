#include "system/SingleInstanceGuard.h"
#include <QLocalServer>
#include <QLocalSocket>
SingleInstanceGuard::SingleInstanceGuard(QObject* p)
    : QObject(p), server_(new QLocalServer(this)) {}
bool SingleInstanceGuard::acquire() {
    const QString name = "ZloWallpaper.SingleInstance.v1";
    if (server_->listen(name)) {
        connect(server_, &QLocalServer::newConnection, this, [this] {
            while (auto* s = server_->nextPendingConnection()) {
                connect(s, &QLocalSocket::readyRead, this, [this, s] {
                    s->readAll();
                    emit activationRequested();
                    s->disconnectFromServer();
                });
            }
        });
        return true;
    }
    QLocalSocket client;
    client.connectToServer(name);
    if (client.waitForConnected(500)) {
        client.write("activate");
        client.waitForBytesWritten(500);
    }
    return false;
}
