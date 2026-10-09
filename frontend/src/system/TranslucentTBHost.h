#pragma once

#include "common/Result.h"
#include <QObject>

class QProcess;

// Hosts the official TranslucentTB portable binary as a narrowly-scoped child
// process. LightWallpaper owns the UI preference and lifetime; the helper's
// tray is explicitly hidden in its generated configuration.
class TranslucentTBHost final : public QObject {
    Q_OBJECT
  public:
    explicit TranslucentTBHost(QObject* parent = nullptr);

    bool enabled() const {
        return enabled_;
    }
    Result setEnabled(bool enabled);
    void startIfEnabled();
    void shutdown();

  signals:
    void statusChanged(const QString& status);

  private:
    QString runtimeDirectory() const;
    QString executablePath() const;
    bool writeConfiguration(QString* failureReason) const;
    void start();

    QProcess* process_ = nullptr;
    bool enabled_ = false;
    bool stopping_ = false;
};
