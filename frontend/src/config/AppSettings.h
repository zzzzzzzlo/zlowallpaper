#pragma once
#include "library/WallpaperEntry.h"
#include <QByteArray>
#include <QString>

class AppSettings final {
  public:
    bool restoreLastWallpaper() const;
    void setRestoreLastWallpaper(bool value);
    bool minimizeToTray() const;
    void setMinimizeToTray(bool value);
    bool muteVideos() const;
    void setMuteVideos(bool value);
    bool pauseWhenInactive() const;
    void setPauseWhenInactive(bool value);
    bool loggingEnabled() const;
    void setLoggingEnabled(bool value);
    DisplayMode displayMode() const;
    void setDisplayMode(DisplayMode value);
    QString activeWallpaperId() const;
    void setActiveWallpaperId(const QString& id);
    QString originalSystemWallpaper() const;
    void setOriginalSystemWallpaper(const QString& path);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& value);
    bool icontraEnabled() const;
    void setIcontraEnabled(bool value);
    bool translucentTbEnabled() const;
    void setTranslucentTbEnabled(bool value);
};
