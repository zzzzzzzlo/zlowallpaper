#include "system/MonitorManager.h"
#include <QGuiApplication>
#include <QScreen>
MonitorManager::MonitorManager(QObject* p) : QObject(p) {
    for (auto* s : QGuiApplication::screens())
        connect(s, &QScreen::geometryChanged, this, &MonitorManager::notify);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* s) {
        connect(s, &QScreen::geometryChanged, this, &MonitorManager::notify);
        notify();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, &MonitorManager::notify);
}
QRect MonitorManager::virtualGeometry() const {
    QRect all;
    for (auto* s : QGuiApplication::screens())
        all = all.united(s->geometry());
    return all;
}
void MonitorManager::notify() {
    emit topologyChanged();
}
