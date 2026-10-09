#pragma once
#include "library/WallpaperEntry.h"
#include <QPixmap>
#include <QProcess>
#include <QWidget>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QMovie;
class QTimer;
class QVideoWidget;

class PreviewWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void preview(const WallpaperEntry* entry);
    void clear();
    void setPlaybackActive(bool active);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void clearContent();
    void showImagePreview();
    void showAnimatedPreview();
    void scheduleVideoPreview();
    void startVideoProxyEncoder(bool softwareFallback);
    void handleVideoProxyFinished(int exitCode, QProcess::ExitStatus status);
    void playVideoProxy(const QString& path);
    void stopVideoPreview(bool cancelEncoding);
    QString previewProxyPathFor(const WallpaperEntry& entry) const;
    void updateGeometry();

    QLabel* image_ = nullptr;
    QVideoWidget* video_ = nullptr;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audio_ = nullptr;
    QMovie* movie_ = nullptr;
    QProcess* proxyEncoder_ = nullptr;
    QTimer* proxyDelay_ = nullptr;
    QPixmap source_;
    WallpaperEntry currentEntry_;
    QString pendingProxyPath_;
    QString pendingProxyTempPath_;
    bool proxySoftwareFallback_ = false;
    bool hasCurrentEntry_ = false;
    bool playbackActive_ = true;
};
