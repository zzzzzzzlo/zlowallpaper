#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>

class PowerEventHandler final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit PowerEventHandler(QObject* parent = nullptr);
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
  signals:
    void suspendRequested();
    void resumeRequested();
};
