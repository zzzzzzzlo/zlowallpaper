#pragma once
#include "common/Result.h"
#include "library/WallpaperEntry.h"
#include <QProcess>
#include <QWidget>
class QLabel;
class QMovie;
class NativeVideoPlayer;
class QTimer;

class WallpaperSurface final : public QWidget {
    Q_OBJECT
  public:
    explicit WallpaperSurface(QWidget* parent = nullptr);
    Result load(const WallpaperEntry& entry);
    void stop();
    void pause(bool paused);
    void setDisplayMode(DisplayMode mode);
    void synchronizeNativeChildren();
    bool isDynamic() const {
        return dynamic_;
    }
  signals:
    void playbackError(const QString& message);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void clearRenderer();
    void placeImage();
    Result startVideoPlayback(const QString& path, const WallpaperEntry& entry,
                              bool evaluateProxy = false);
    void prepareVideoProxy(const WallpaperEntry& entry, const QSize& sourceSize);
    void startProxyEncoder(bool softwareFallback);
    void startProxyEncoderWhenIdle();
    void handleProxyEncoderFinished(int exitCode, QProcess::ExitStatus status);
    QString proxyPathFor(const WallpaperEntry& entry) const;
    QLabel* image_ = nullptr;
    QMovie* movie_ = nullptr;
    NativeVideoPlayer* video_ = nullptr;
    QProcess* proxyEncoder_ = nullptr;
    QTimer* proxyIdleTimer_ = nullptr;
    WallpaperEntry proxyEntry_;
    QString pendingProxyPath_;
    QString pendingProxyTempPath_;
    bool proxySoftwareFallback_ = false;
    int proxyIdleWaitAttempts_ = 0;
    QPixmap source_;
    DisplayMode mode_ = DisplayMode::Fill;
    bool dynamic_ = false;
};
