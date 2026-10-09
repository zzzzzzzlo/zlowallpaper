#pragma once
#include <QObject>
class QLocalServer;
class SingleInstanceGuard final : public QObject {
    Q_OBJECT
  public:
    explicit SingleInstanceGuard(QObject* parent = nullptr);
    bool acquire();
  signals:
    void activationRequested();

  private:
    QLocalServer* server_ = nullptr;
};
