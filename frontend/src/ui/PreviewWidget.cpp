#include "ui/PreviewWidget.h"
#include <QAudioOutput>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMediaPlayer>
#include <QMovie>
#include <QProcess>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVideoWidget>
#include <algorithm>
#include <windows.h>

namespace {
constexpr QSize PreviewResolution(640, 360);
constexpr int PreviewFrameRate = 30;
constexpr int PreviewStartDelayMs = 350;
constexpr qint64 PreviewCacheLimitBytes = 256LL * 1024 * 1024;
constexpr int PreviewCacheLimitFiles = 32;

void trimPreviewCache(const QString& directory, const QString& preservePath) {
    QDir cache(directory);
    auto files = cache.entryInfoList({"*.mp4"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 totalBytes = 0;
    for (const auto& file : files)
        totalBytes += file.size();

    while ((totalBytes > PreviewCacheLimitBytes || files.size() > PreviewCacheLimitFiles) &&
           !files.isEmpty()) {
        const auto removable =
            std::find_if(files.cbegin(), files.cend(), [&preservePath](const QFileInfo& file) {
                return file.absoluteFilePath() != preservePath;
            });
        if (removable == files.cend())
            break;
        const auto path = removable->absoluteFilePath();
        const auto bytes = removable->size();
        if (QFile::remove(path))
            totalBytes -= bytes;
        files.erase(removable);
    }
}
} // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setObjectName("previewFrame");
    setStyleSheet(
        "#previewFrame { background: #ffffff; border: 1px solid #d9e3ef; border-radius: 12px; }");
    setToolTip("视频预览使用 640×360、30fps 的轻量代理。");

    image_ = new QLabel(this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setStyleSheet("color: #71859b; font-size: 13px; border: none;");

    video_ = new QVideoWidget(this);
    video_->setStyleSheet("background: #ffffff; border: none;");
    video_->hide();

    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    audio_->setMuted(true);
    player_->setAudioOutput(audio_);
    player_->setVideoOutput(video_);
    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia && playbackActive_ && hasCurrentEntry_ &&
                    currentEntry_.type == WallpaperType::Video) {
                    player_->setPosition(0);
                    player_->play();
                }
            });
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString&) {
                if (!hasCurrentEntry_ || currentEntry_.type != WallpaperType::Video)
                    return;
                stopVideoPreview(false);
                showImagePreview();
            });

    proxyDelay_ = new QTimer(this);
    proxyDelay_->setSingleShot(true);
    connect(proxyDelay_, &QTimer::timeout, this, [this] {
        if (playbackActive_ && hasCurrentEntry_ && currentEntry_.type == WallpaperType::Video &&
            !pendingProxyPath_.isEmpty() && !QFileInfo::exists(pendingProxyPath_)) {
            startVideoProxyEncoder(false);
        }
    });

    clear();
}

void PreviewWidget::clearContent() {
    stopVideoPreview(true);
    if (movie_) {
        movie_->stop();
        image_->setMovie(nullptr);
        delete movie_;
        movie_ = nullptr;
    }
    source_ = {};
    image_->clear();
    image_->show();
}

void PreviewWidget::clear() {
    clearContent();
    hasCurrentEntry_ = false;
    currentEntry_ = {};
    image_->setText("选择一张壁纸以查看预览");
    updateGeometry();
}

void PreviewWidget::showImagePreview() {
    image_->show();
    source_.load(currentEntry_.thumbnailPath);
    if (source_.isNull() && currentEntry_.type != WallpaperType::Video)
        source_.load(currentEntry_.path);
    if (source_.isNull()) {
        image_->setText("正在准备预览封面");
        return;
    }
    image_->setText({});
    updateGeometry();
}

QString PreviewWidget::previewProxyPathFor(const WallpaperEntry& entry) const {
    const QFileInfo source(entry.path);
    if (!source.exists())
        return {};
    const auto fingerprint = QString("%1|%2|%3|preview-%4x%5-%6")
                                 .arg(source.canonicalFilePath())
                                 .arg(source.size())
                                 .arg(source.lastModified().toMSecsSinceEpoch())
                                 .arg(PreviewResolution.width())
                                 .arg(PreviewResolution.height())
                                 .arg(PreviewFrameRate);
    const auto key =
        QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/preview-proxies";
    if (!QDir().mkpath(directory))
        return {};
    const auto path = directory + "/" + QString::fromLatin1(key) + ".mp4";
    trimPreviewCache(directory, path);
    return path;
}

void PreviewWidget::showAnimatedPreview() {
    if (currentEntry_.type == WallpaperType::Video) {
        showImagePreview();
        scheduleVideoPreview();
        return;
    }

    movie_ = new QMovie(currentEntry_.path, {}, this);
    movie_->setCacheMode(QMovie::CacheNone);
    if (!movie_->isValid()) {
        delete movie_;
        movie_ = nullptr;
        image_->setText("无法播放动态预览");
        return;
    }
    image_->setMovie(movie_);
    updateGeometry();
    if (playbackActive_)
        movie_->start();
}

void PreviewWidget::scheduleVideoPreview() {
    if (!playbackActive_ || currentEntry_.type != WallpaperType::Video)
        return;
    pendingProxyPath_ = previewProxyPathFor(currentEntry_);
    if (pendingProxyPath_.isEmpty())
        return;
    if (QFileInfo(pendingProxyPath_).size() > 0) {
        playVideoProxy(pendingProxyPath_);
        return;
    }
    proxyDelay_->start(PreviewStartDelayMs);
}

void PreviewWidget::startVideoProxyEncoder(bool softwareFallback) {
    if (!playbackActive_ || proxyEncoder_ || pendingProxyPath_.isEmpty())
        return;
    const auto executable = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (!QFileInfo::exists(executable))
        return;

    pendingProxyTempPath_ = pendingProxyPath_ + ".partial.mp4";
    QFile::remove(pendingProxyTempPath_);
    proxySoftwareFallback_ = softwareFallback;
    auto* encoder = new QProcess(this);
    proxyEncoder_ = encoder;
    connect(encoder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, encoder](int exitCode, QProcess::ExitStatus status) {
                if (encoder == proxyEncoder_)
                    handleVideoProxyFinished(exitCode, status);
            });
    connect(encoder, &QProcess::errorOccurred, this, [this, encoder](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || encoder != proxyEncoder_)
            return;
        proxyEncoder_ = nullptr;
        encoder->deleteLater();
        if (!proxySoftwareFallback_)
            startVideoProxyEncoder(true);
    });
    connect(encoder, &QProcess::started, this, [this, encoder] {
        if (encoder != proxyEncoder_)
            return;
        if (const auto handle =
                OpenProcess(PROCESS_SET_INFORMATION, FALSE, DWORD(encoder->processId()))) {
            SetPriorityClass(handle, IDLE_PRIORITY_CLASS);
            CloseHandle(handle);
        }
    });

    const auto filter = QString("fps=%1,scale=w=%2:h=%3:force_original_aspect_ratio=decrease:force_"
                                "divisible_by=2,pad=%2:%3:(ow-iw)/2:(oh-ih)/2")
                            .arg(PreviewFrameRate)
                            .arg(PreviewResolution.width())
                            .arg(PreviewResolution.height());
    QStringList arguments{
        "-hide_banner",     "-loglevel", "error", "-nostdin", "-y",  "-ss", "00:00:01", "-i",
        currentEntry_.path, "-map",      "0:v:0", "-an",      "-sn", "-dn", "-vf",      filter};
    if (softwareFallback) {
        arguments << "-c:v"
                  << "libx264"
                  << "-preset"
                  << "veryfast"
                  << "-crf"
                  << "24";
    } else {
        arguments << "-c:v"
                  << "h264_nvenc"
                  << "-preset"
                  << "p4"
                  << "-tune"
                  << "hq"
                  << "-rc"
                  << "vbr"
                  << "-cq"
                  << "25"
                  << "-b:v"
                  << "0";
    }
    arguments << "-pix_fmt"
              << "yuv420p"
              << "-movflags"
              << "+faststart" << pendingProxyTempPath_;
    encoder->start(executable, arguments);
}

void PreviewWidget::handleVideoProxyFinished(int exitCode, QProcess::ExitStatus status) {
    auto* finishedProcess = proxyEncoder_;
    proxyEncoder_ = nullptr;
    if (finishedProcess)
        finishedProcess->deleteLater();
    if (status != QProcess::NormalExit || exitCode != 0 ||
        QFileInfo(pendingProxyTempPath_).size() <= 0) {
        QFile::remove(pendingProxyTempPath_);
        if (!proxySoftwareFallback_)
            startVideoProxyEncoder(true);
        return;
    }
    QFile::remove(pendingProxyPath_);
    if (!QFile::rename(pendingProxyTempPath_, pendingProxyPath_))
        return;
    trimPreviewCache(QFileInfo(pendingProxyPath_).absolutePath(), pendingProxyPath_);
    if (playbackActive_ && hasCurrentEntry_ && currentEntry_.type == WallpaperType::Video)
        playVideoProxy(pendingProxyPath_);
}

void PreviewWidget::playVideoProxy(const QString& path) {
    if (!playbackActive_ || !QFileInfo::exists(path))
        return;
    proxyDelay_->stop();
    image_->hide();
    video_->show();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
}

void PreviewWidget::stopVideoPreview(bool cancelEncoding) {
    player_->stop();
    player_->setSource({});
    video_->hide();
    image_->show();
    if (!cancelEncoding)
        return;
    proxyDelay_->stop();
    if (proxyEncoder_) {
        proxyEncoder_->kill();
        proxyEncoder_->deleteLater();
        proxyEncoder_ = nullptr;
    }
    if (!pendingProxyTempPath_.isEmpty())
        QFile::remove(pendingProxyTempPath_);
    pendingProxyPath_.clear();
    pendingProxyTempPath_.clear();
    proxySoftwareFallback_ = false;
}

void PreviewWidget::preview(const WallpaperEntry* entry) {
    clearContent();
    hasCurrentEntry_ = entry && entry->valid;
    if (!hasCurrentEntry_) {
        image_->setText("选择一张壁纸以查看预览");
        updateGeometry();
        return;
    }

    currentEntry_ = *entry;
    if (currentEntry_.type == WallpaperType::Video || currentEntry_.type == WallpaperType::Gif)
        showAnimatedPreview();
    else
        showImagePreview();
}

void PreviewWidget::setPlaybackActive(bool active) {
    if (playbackActive_ == active)
        return;
    playbackActive_ = active;
    if (!hasCurrentEntry_)
        return;
    if (!active) {
        stopVideoPreview(true);
        if (movie_)
            movie_->stop();
        showImagePreview();
        return;
    }
    if (currentEntry_.type == WallpaperType::Video)
        scheduleVideoPreview();
    else if (movie_) {
        movie_->start();
    }
}

void PreviewWidget::updateGeometry() {
    image_->setGeometry(rect());
    video_->setGeometry(rect());
    if (!source_.isNull())
        image_->setPixmap(source_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (movie_ && movie_->frameRect().isValid())
        movie_->setScaledSize(movie_->frameRect().size().scaled(size(), Qt::KeepAspectRatio));
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateGeometry();
}
