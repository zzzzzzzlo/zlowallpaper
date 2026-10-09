#pragma once
#include <QObject>
#include <QRect>
class MonitorManager final : public QObject {
    Q_OBJECT
  public:
    explicit MonitorManager(QObject* parent = nullptr);
    QRect virtualGeometry() const;
  signals:
    void topologyChanged();
  private slots:
    void notify();
};
