#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;
class QProcess;
class QTimer;

// Owns the managed Electron Dock process and its authenticated local IPC link.
// The Dock remains an independent top-level window; it is never embedded in Qt.
class IcontraHost final : public QObject {
    Q_OBJECT
  public:
    explicit IcontraHost(QObject* parent = nullptr);
    ~IcontraHost() override;

    void start();
    void shutdown(int timeoutMs = 2500);
    void setEnabled(bool enabled);
    bool enabled() const {
        return enabled_;
    }
    bool ready() const {
        return ready_;
    }
    qint64 processId() const;
    QJsonObject dockState() const {
        return dockState_;
    }

    void showDock();
    void hideDock();
    void toggleDock();
    void updateDockSettings(const QJsonObject& settings);
    void addDockApplications(const QStringList& paths);
    void removeDockApplication(const QString& id);
    void moveDockApplication(const QString& id, int destinationIndex);
    void resetDockPosition();
    void suspend();
    void resume();
    // The Qt host is the single fullscreen authority in managed mode.  Keep
    // a separate visibility memory so leaving fullscreen never re-shows a
    // Dock the user had deliberately hidden.
    void setFullscreenActive(bool active);
    void recoverAfterExplorer();

  signals:
    void statusChanged(const QString& status);
    void readyChanged(bool ready);
    void fullscreenChanged(bool active);
    void warning(const QString& message);
    void dockStateChanged(const QJsonObject& state);

  private:
    struct LaunchCommand {
        QString program;
        QStringList arguments;
        QString workingDirectory;
    };

    bool openServer();
    LaunchCommand resolveLaunchCommand() const;
    void startChild();
    void scheduleRestart(const QString& reason);
    void stopOwnedChild();
    void assignChildToJob();
    void setReady(bool ready);
    void setStatus(const QString& status);
    bool keepVisibleOverFullscreen() const;
    void send(const QString& type, const QJsonObject& payload = {}, const QString& requestId = {});
    void handleMessage(const QJsonObject& message);
    void consumeSocket();
    void closeServer();

    QLocalServer* server_ = nullptr;
    QLocalSocket* socket_ = nullptr;
    QProcess* process_ = nullptr;
    QTimer* shutdownTimer_ = nullptr;
    QTimer* restartTimer_ = nullptr;
    QString pipeName_;
    QString token_;
    QString receiveBuffer_;
    bool enabled_ = true;
    bool ready_ = false;
    bool authenticated_ = false;
    bool shuttingDown_ = false;
    bool fullscreenActive_ = false;
    bool wasVisibleBeforeSuspend_ = false;
    bool wasVisibleBeforeFullscreen_ = false;
    bool hostFullscreenActive_ = false;
    bool dockVisible_ = false;
    QJsonObject dockState_;
    int restartAttempts_ = 0;
    void* job_ = nullptr;
};
