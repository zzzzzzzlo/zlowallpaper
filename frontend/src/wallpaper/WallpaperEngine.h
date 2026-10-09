#pragma once
#include "common/Result.h"
#include "library/WallpaperEntry.h"
#include <QObject>

class DesktopHost;
class WallpaperSurface;
class MonitorManager;
class QTimer;

class WallpaperEngine final : public QObject {
    Q_OBJECT
  public:
    explicit WallpaperEngine(MonitorManager* monitors, QObject* parent = nullptr);
    ~WallpaperEngine() override;

    Result apply(const WallpaperEntry& entry);
    void stop();
    void pause(bool paused);
    void suspend();
    void recoverDesktop();

    bool active() const {
        return active_;
    }
    QString activeId() const {
        return activeId_;
    }

  signals:
    void failed(const Result& result);
    void stopped();

  private:
    void createSurface();
    void disposeSurface(bool restoreTopLevel = true);
    Result attachAndLoad(const WallpaperEntry& entry);
    void scheduleRecovery(int delayMs = 1200);
    void rebuildAfterDesktopChange();
    void handlePlaybackError(const QString& message);
    void armPlaybackStabilityReset();
    void rememberSystemWallpaper();

    DesktopHost* host_ = nullptr;
    WallpaperSurface* surface_ = nullptr;
    MonitorManager* monitors_ = nullptr;
    QTimer* recoveryTimer_ = nullptr;
    QTimer* playbackStabilityTimer_ = nullptr;
    WallpaperEntry activeEntry_;
    QString activeId_;
    int recoveryAttempts_ = 0;
    int playbackErrorRecoveryAttempts_ = 0;
    bool active_ = false;
    bool suspended_ = false;
};
