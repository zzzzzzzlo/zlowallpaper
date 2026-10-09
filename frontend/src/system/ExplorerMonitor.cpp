#include "system/ExplorerMonitor.h"
#include "common/Logger.h"
#include <QTimer>
#include <windows.h>
ExplorerMonitor::ExplorerMonitor(QObject* p) : QObject(p), timer_(new QTimer(this)) {
    timer_->setInterval(2000);
    connect(timer_, &QTimer::timeout, this, [this] {
        // Modern Standby does not always produce a traditional suspend event
        // for a Qt window. A large timer gap is a reliable second signal that
        // the desktop may have been reconstructed while the app was asleep.
        const bool wokeFromLongGap = heartbeat_.isValid() && heartbeat_.elapsed() > 7000;
        heartbeat_.restart();
        const auto now = reinterpret_cast<quintptr>(FindWindowW(L"Progman", nullptr));
        if (now != lastProgman_ || wokeFromLongGap) {
            lastProgman_ = now;
            if (wokeFromLongGap)
                Logger::info("Desktop monitor detected a post-sleep timer gap");
            emit desktopMayHaveChanged();
        }
    });
}
void ExplorerMonitor::start() {
    lastProgman_ = reinterpret_cast<quintptr>(FindWindowW(L"Progman", nullptr));
    heartbeat_.start();
    timer_->start();
}
void ExplorerMonitor::stop() {
    timer_->stop();
}
