#include "system/PowerEventHandler.h"
#include "common/Logger.h"
#include <windows.h>
PowerEventHandler::PowerEventHandler(QObject* parent) : QObject(parent) {}
bool PowerEventHandler::nativeEventFilter(const QByteArray&, void* message, qintptr*) {
    const auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_POWERBROADCAST)
        return false;
    if (msg->wParam == PBT_APMSUSPEND) {
        Logger::info("Received PBT_APMSUSPEND");
        emit suspendRequested();
    }
    if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND ||
        msg->wParam == PBT_APMRESUMECRITICAL) {
        Logger::info(QString("Received power resume notification: %1").arg(msg->wParam));
        emit resumeRequested();
    }
    return false;
}
