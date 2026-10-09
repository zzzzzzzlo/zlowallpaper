#pragma once
#include <QElapsedTimer>
#include <QObject>
class QTimer;
class ExplorerMonitor final : public QObject {
    Q_OBJECT
  public:
    explicit ExplorerMonitor(QObject* parent = nullptr);
    void start();
    void stop();
  signals:
    void desktopMayHaveChanged();

  private:
    QTimer* timer_;
    quintptr lastProgman_ = 0;
    QElapsedTimer heartbeat_;
};
