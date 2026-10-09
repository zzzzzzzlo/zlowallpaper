#pragma once
#include <QObject>
class WallpaperLibrary;
class WallpaperEngine;
class MonitorManager;
class ExplorerMonitor;
class PowerEventHandler;
class MainWindow;
class IcontraHost;
class TranslucentTBHost;
class QTimer;
class Application final : public QObject {
    Q_OBJECT
  public:
    explicit Application(QObject* parent = nullptr);
    bool start();
    void startSmokeWallpaper();
    void recoverSmokeWallpaper();
    void startIcontraSmoke();

  private:
    void syncPauseState();
    WallpaperLibrary* library_;
    MonitorManager* monitors_;
    WallpaperEngine* engine_;
    ExplorerMonitor* explorer_;
    PowerEventHandler* power_;
    IcontraHost* icontra_;
    TranslucentTBHost* translucentTb_;
    MainWindow* window_;
    QTimer* activityTimer_;
    bool autoPaused_ = false;
    bool dockFullscreen_ = false;
};
