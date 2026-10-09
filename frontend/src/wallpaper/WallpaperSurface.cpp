#include "wallpaper/WallpaperSurface.h"
#include "common/Logger.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMetaObject>
#include <QMovie>
#include <QPainter>
#include <QProcess>
#include <QResizeEvent>
#include <QSize>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <dxgi1_6.h>
#include <functional>
#include <memory>
#include <mpv/client.h>
#include <mutex>
#include <vector>
#include <windows.h>

namespace {

constexpr qint64 ProxyCacheLimitBytes = 1024LL * 1024 * 1024;
constexpr int ProxyCacheLimitFiles = 12;

QString mpvErrorText(int error) {
    return QString::fromUtf8(mpv_error_string(error));
}

QString preferredD3d11Adapter() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return {};

    SIZE_T largestDedicatedMemory = 0;
    QString adapterName;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!adapter)
            continue;
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)) &&
            !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            description.DedicatedVideoMemory > largestDedicatedMemory) {
            largestDedicatedMemory = description.DedicatedVideoMemory;
            adapterName = QString::fromWCharArray(description.Description);
        }
        adapter->Release();
    }
    factory->Release();
    return adapterName;
}

QSize nativeClientSize(HWND window) {
    RECT clientRect{};
    if (!window || !GetClientRect(window, &clientRect))
        return {};
    return QSize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
}

void trimProxyCache(const QString& directory, const QString& preservePath) {
    QDir cache(directory);
    auto files = cache.entryInfoList({"*.mp4"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 totalBytes = 0;
    for (const auto& file : files)
        totalBytes += file.size();

    while ((totalBytes > ProxyCacheLimitBytes || files.size() > ProxyCacheLimitFiles) &&
           !files.isEmpty()) {
        const auto removable =
            std::find_if(files.cbegin(), files.cend(), [&preservePath](const QFileInfo& file) {
                return file.absoluteFilePath() != preservePath;
            });
        if (removable == files.cend())
            break;
        const auto removedPath = removable->absoluteFilePath();
        const auto removedBytes = removable->size();
        if (QFile::remove(removedPath)) {
            totalBytes -= removedBytes;
            Logger::info("Removed old video proxy: " + removedPath);
        }
        files.erase(removable);
    }
}

struct MpvState {
    std::mutex mutex;
    mpv_handle* player = nullptr;
    std::function<void(const QString&)> errorHandler;
    std::function<void(const QSize&)> videoLoadedHandler;
    std::atomic_bool alive{true};
    std::atomic_bool eventQueued{false};
    QSize presentationSize;
    bool adaptiveScaleConfigured = false;
};

struct MpvWakeupContext {
    std::weak_ptr<MpvState> state;
};

QSize sourceVideoSize(MpvState* state) {
    if (!state || !state->player)
        return {};

    int64_t sourceWidth = 0;
    int64_t sourceHeight = 0;
    if (mpv_get_property(state->player, "video-params/w", MPV_FORMAT_INT64, &sourceWidth) < 0 ||
        mpv_get_property(state->player, "video-params/h", MPV_FORMAT_INT64, &sourceHeight) < 0 ||
        sourceWidth <= 0 || sourceHeight <= 0) {
        return {};
    }
    return QSize(int(sourceWidth), int(sourceHeight));
}

void configureAdaptiveVideoScale(MpvState* state, const QSize& sourceSize) {
    if (!state || state->adaptiveScaleConfigured || !state->presentationSize.isValid() ||
        !sourceSize.isValid())
        return;

    const auto scale =
        std::min(double(state->presentationSize.width()) / double(sourceSize.width()),
                 double(state->presentationSize.height()) / double(sourceSize.height()));
    state->adaptiveScaleConfigured = true;
    if (scale >= 0.999)
        return;

    // Let the D3D11 Video Processor downscale oversized video before mpv's
    // 3D renderer sees it. This keeps the visible output at the monitor's
    // resolution, preserves the source frame rate, and avoids a 4K texture
    // conversion for every frame on lower-resolution desktops.
    const auto filter = QString("d3d11vpp=scale=%1").arg(scale, 0, 'f', 6).toUtf8();
    if (mpv_set_property_string(state->player, "vf", filter.constData()) >= 0) {
        Logger::info(QString("mpv adaptive video scale: %1x%2 -> %3x%4")
                         .arg(sourceSize.width())
                         .arg(sourceSize.height())
                         .arg(qRound(sourceSize.width() * scale))
                         .arg(qRound(sourceSize.height() * scale)));
    } else {
        Logger::warning("mpv adaptive video scale was rejected; using native video resolution");
    }
}

void drainMpvEvents(const std::shared_ptr<MpvState>& state) {
    state->eventQueued.store(false);
    QString error;
    std::function<void(const QString&)> errorHandler;
    std::function<void(const QSize&)> videoLoadedHandler;
    QSize loadedVideoSize;
    {
        std::lock_guard lock(state->mutex);
        if (!state->alive || !state->player)
            return;

        while (true) {
            const auto* event = mpv_wait_event(state->player, 0.0);
            if (!event || event->event_id == MPV_EVENT_NONE)
                break;

            if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == 1 &&
                event->error < 0) {
                error = QString("mpv 无法载入视频：%1").arg(mpvErrorText(event->error));
                break;
            }
            if (event->event_id == MPV_EVENT_FILE_LOADED) {
                loadedVideoSize = sourceVideoSize(state.get());
                configureAdaptiveVideoScale(state.get(), loadedVideoSize);
                videoLoadedHandler = state->videoLoadedHandler;
            }
            if (event->event_id == MPV_EVENT_END_FILE) {
                const auto* endFile = static_cast<const mpv_event_end_file*>(event->data);
                if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR) {
                    error = QString("mpv 视频解码失败：%1").arg(mpvErrorText(endFile->error));
                    break;
                }
            }
            if (event->event_id == MPV_EVENT_SHUTDOWN && state->alive) {
                error = "mpv 播放器意外终止";
                break;
            }
        }
        errorHandler = state->errorHandler;
    }
    if (loadedVideoSize.isValid() && videoLoadedHandler)
        videoLoadedHandler(loadedVideoSize);
    if (!error.isEmpty() && errorHandler)
        errorHandler(error);
}

void onMpvWakeup(void* context) {
    auto* wakeup = static_cast<MpvWakeupContext*>(context);
    if (!wakeup)
        return;
    const auto state = wakeup->state.lock();
    if (!state || !state->alive || state->eventQueued.exchange(true))
        return;

    auto* application = QCoreApplication::instance();
    if (!application) {
        state->eventQueued.store(false);
        return;
    }

    // The callback belongs to mpv's worker thread. Only schedule work here;
    // all client-API calls remain serialized on Qt's main thread.
    QMetaObject::invokeMethod(
        application, [state] { drainMpvEvents(state); }, Qt::QueuedConnection);
}

} // namespace

class NativeVideoPlayer final {
  public:
    using ErrorHandler = std::function<void(const QString&)>;
    using VideoLoadedHandler = std::function<void(const QSize&)>;

    explicit NativeVideoPlayer(HWND target) : target_(target) {}
    ~NativeVideoPlayer() {
        stop();
    }

    int start(const QString& path, bool muted, DisplayMode, ErrorHandler errorHandler,
              VideoLoadedHandler videoLoadedHandler) {
        stop();
        state_ = std::make_shared<MpvState>();
        state_->presentationSize = nativeClientSize(target_);
        wakeup_ = std::make_shared<MpvWakeupContext>();
        wakeup_->state = state_;
        player_ = mpv_create();
        if (!player_)
            return MPV_ERROR_NOMEM;

        const auto windowId = QString::number(reinterpret_cast<quintptr>(target_)).toUtf8();
        const auto adapterName = preferredD3d11Adapter().toUtf8();
        std::vector<std::pair<const char*, const char*>> options{
            {"wid", windowId.constData()},
            // gpu-next uses mpv's modern D3D11 presentation path. The simple
            // filters are deliberate: wallpaper is constantly visible, so
            // expensive scaling shaders would consume the 3D engine without
            // improving a same-resolution video.
            {"vo", "gpu-next"},
            {"gpu-api", "d3d11"},
            {"gpu-context", "d3d11"},
            {"hwdec", "d3d11va"},
            {"scale", "bilinear"},
            {"cscale", "bilinear"},
            {"dscale", "box"},
            {"interpolation", "no"},
            {"loop-file", "inf"},
            {"video-sync", "display-resample"},
            // mpv enables this by default, which translates to a Windows
            // execution-state request while a video is playing. A desktop
            // wallpaper must never keep the display or system awake.
            {"stop-screensaver", "no"},
            {"keep-open", "yes"},
            {"osc", "no"},
            {"input-default-bindings", "no"},
            {"terminal", "no"},
        };
        // Hybrid-GPU laptops otherwise tend to select the desktop's integrated
        // adapter. On systems with a discrete GPU, keep both rendering and
        // D3D11VA decoding on the adapter with the most dedicated memory.
        if (!adapterName.isEmpty())
            options.emplace_back("d3d11-adapter", adapterName.constData());
        for (const auto& [name, value] : options) {
            const int result = mpv_set_option_string(player_, name, value);
            if (result < 0) {
                stop();
                return result;
            }
        }
        if (muted) {
            const int result = mpv_set_option_string(player_, "audio", "no");
            if (result < 0) {
                stop();
                return result;
            }
        }

        int result = mpv_initialize(player_);
        if (result < 0) {
            stop();
            return result;
        }

        {
            std::lock_guard lock(state_->mutex);
            state_->player = player_;
            state_->errorHandler = std::move(errorHandler);
            state_->videoLoadedHandler = std::move(videoLoadedHandler);
        }
        mpv_set_wakeup_callback(player_, onMpvWakeup, wakeup_.get());

        const auto localPath = path.toUtf8();
        const char* command[] = {"loadfile", localPath.constData(), "replace", nullptr};
        result = mpv_command_async(player_, 1, command);
        if (result < 0) {
            stop();
            return result;
        }
        return 0;
    }

    void stop() {
        auto state = std::move(state_);
        auto wakeup = std::move(wakeup_);
        auto* player = player_;
        player_ = nullptr;
        if (!state || !player)
            return;

        state->alive.store(false);
        {
            std::lock_guard lock(state->mutex);
            state->errorHandler = {};
            state->videoLoadedHandler = {};
            state->player = nullptr;
        }
        mpv_set_wakeup_callback(player, nullptr, nullptr);
        mpv_terminate_destroy(player);
    }

    void pause(bool paused) {
        if (!player_)
            return;
        mpv_set_property_string(player_, "pause", paused ? "yes" : "no");
    }

    void updateVideo() {
        // In wid mode mpv owns a child D3D11 window and tracks the native Qt
        // host rectangle without forced resize or decoder reinitialization.
    }

  private:
    HWND target_ = nullptr;
    mpv_handle* player_ = nullptr;
    std::shared_ptr<MpvState> state_;
    std::shared_ptr<MpvWakeupContext> wakeup_;
};

WallpaperSurface::WallpaperSurface(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    image_ = new QLabel(this);
    image_->setAlignment(Qt::AlignCenter);
    image_->hide();
    proxyIdleTimer_ = new QTimer(this);
    proxyIdleTimer_->setSingleShot(true);
    connect(proxyIdleTimer_, &QTimer::timeout, this, &WallpaperSurface::startProxyEncoderWhenIdle);
}

void WallpaperSurface::clearRenderer() {
    if (proxyIdleTimer_)
        proxyIdleTimer_->stop();
    if (proxyEncoder_) {
        proxyEncoder_->kill();
        proxyEncoder_->deleteLater();
        proxyEncoder_ = nullptr;
    }
    pendingProxyPath_.clear();
    pendingProxyTempPath_.clear();
    proxyEntry_ = {};
    proxySoftwareFallback_ = false;
    proxyIdleWaitAttempts_ = 0;
    if (movie_) {
        movie_->stop();
        delete movie_;
        movie_ = nullptr;
    }
    if (video_) {
        delete video_;
        video_ = nullptr;
    }
    image_->clear();
    image_->hide();
    source_ = {};
    dynamic_ = false;
}

void WallpaperSurface::setDisplayMode(DisplayMode mode) {
    mode_ = mode;
    placeImage();
    if (video_)
        video_->updateVideo();
}

void WallpaperSurface::placeImage() {
    if (source_.isNull())
        return;
    image_->setGeometry(rect());
    if (mode_ == DisplayMode::Tile) {
        QPixmap tile(size());
        tile.fill(Qt::black);
        QPainter painter(&tile);
        for (int y = 0; y < height(); y += source_.height())
            for (int x = 0; x < width(); x += source_.width())
                painter.drawPixmap(x, y, source_);
        image_->setPixmap(tile);
        return;
    }
    const auto aspectRatio = mode_ == DisplayMode::Fit || mode_ == DisplayMode::Center
                                 ? Qt::KeepAspectRatio
                                 : Qt::KeepAspectRatioByExpanding;
    const auto scaled =
        mode_ == DisplayMode::Stretch
            ? source_.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            : source_.scaled(size(), aspectRatio, Qt::SmoothTransformation);
    image_->setPixmap(scaled);
}

Result WallpaperSurface::startVideoPlayback(const QString& path, const WallpaperEntry& entry,
                                            bool evaluateProxy) {
    if (!video_)
        video_ = new NativeVideoPlayer(reinterpret_cast<HWND>(winId()));
    const auto result = video_->start(
        path, entry.muted, mode_, [this](const QString& error) { emit playbackError(error); },
        [this, entry, evaluateProxy](const QSize& sourceSize) {
            if (evaluateProxy)
                prepareVideoProxy(entry, sourceSize);
        });
    if (result < 0) {
        const auto message = QString("无法初始化 mpv 硬件视频播放：%1").arg(mpvErrorText(result));
        return Result::failure(WallpaperError::PlaybackFailed, message);
    }
    video_->updateVideo();
    return Result::success();
}

QString WallpaperSurface::proxyPathFor(const WallpaperEntry& entry) const {
    const auto target = nativeClientSize(reinterpret_cast<HWND>(winId()));
    const QFileInfo source(entry.path);
    if (!target.isValid() || !source.exists())
        return {};
    const auto fingerprint = QString("%1|%2|%3|%4x%5")
                                 .arg(source.canonicalFilePath())
                                 .arg(source.size())
                                 .arg(source.lastModified().toMSecsSinceEpoch())
                                 .arg(target.width())
                                 .arg(target.height());
    const auto key =
        QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/proxies";
    if (!QDir().mkpath(directory))
        return {};
    const auto path = directory + "/" + QString::fromLatin1(key) + ".mp4";
    trimProxyCache(directory, path);
    return path;
}

void WallpaperSurface::prepareVideoProxy(const WallpaperEntry& entry, const QSize& sourceSize) {
    const auto target = nativeClientSize(reinterpret_cast<HWND>(winId()));
    if (!sourceSize.isValid() || !target.isValid())
        return;
    if (sourceSize.width() <= target.width() && sourceSize.height() <= target.height()) {
        Logger::info(QString("Video proxy skipped: source %1x%2 fits display %3x%4")
                         .arg(sourceSize.width())
                         .arg(sourceSize.height())
                         .arg(target.width())
                         .arg(target.height()));
        return;
    }

    pendingProxyPath_ = proxyPathFor(entry);
    if (pendingProxyPath_.isEmpty())
        return;
    proxyEntry_ = entry;
    if (QFileInfo(pendingProxyPath_).size() > 0) {
        const auto result = startVideoPlayback(pendingProxyPath_, entry);
        if (result.ok)
            Logger::info("Using display-resolution video proxy: " + pendingProxyPath_);
        return;
    }
    // ffmpeg is deliberately deferred out of the wallpaper/Dock startup
    // burst. Playback continues from the original file until the low-priority
    // proxy is ready, so this never delays a wallpaper becoming visible.
    proxyIdleWaitAttempts_ = 0;
    proxyIdleTimer_->start(4000);
    Logger::info("Video proxy queued until the desktop is idle");
}

void WallpaperSurface::startProxyEncoderWhenIdle() {
    LASTINPUTINFO lastInput{sizeof(LASTINPUTINFO)};
    const DWORD now = GetTickCount();
    const bool hasBeenIdle =
        GetLastInputInfo(&lastInput) && static_cast<DWORD>(now - lastInput.dwTime) >= 2500;
    // Do not starve the optimization forever on an actively used desktop.
    // At most thirty seconds of deferral keeps the source video fallback
    // bounded while protecting the first interactive moments after startup.
    if (!hasBeenIdle && ++proxyIdleWaitAttempts_ < 14) {
        proxyIdleTimer_->start(2000);
        return;
    }
    startProxyEncoder(false);
}

void WallpaperSurface::startProxyEncoder(bool softwareFallback) {
    if (proxyEncoder_ || proxyEntry_.path.isEmpty() || pendingProxyPath_.isEmpty())
        return;
    const auto executable = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (!QFileInfo::exists(executable)) {
        Logger::warning("ffmpeg runtime is unavailable; playing original video");
        return;
    }
    const auto target = nativeClientSize(reinterpret_cast<HWND>(winId()));
    if (!target.isValid())
        return;

    pendingProxyTempPath_ = pendingProxyPath_ + ".partial.mp4";
    QFile::remove(pendingProxyTempPath_);
    proxySoftwareFallback_ = softwareFallback;
    auto* encoder = new QProcess(this);
    proxyEncoder_ = encoder;
    connect(encoder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, encoder](int exitCode, QProcess::ExitStatus status) {
                if (encoder == proxyEncoder_)
                    handleProxyEncoderFinished(exitCode, status);
            });
    connect(encoder, &QProcess::errorOccurred, this, [this, encoder](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || encoder != proxyEncoder_)
            return;
        const auto reason = encoder->errorString();
        proxyEncoder_ = nullptr;
        encoder->deleteLater();
        Logger::warning("Unable to start video proxy encoder: " + reason);
        if (!proxySoftwareFallback_) {
            Logger::warning("Hardware proxy encoder did not start; retrying at idle CPU priority");
            startProxyEncoder(true);
        } else {
            Logger::warning("Software proxy encoder did not start; keeping original playback");
        }
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
    const auto filter = QString("scale=w='min(iw,%1)':h='min(ih,%2)':force_original_aspect_ratio="
                                "decrease:force_divisible_by=2")
                            .arg(target.width())
                            .arg(target.height());
    QStringList arguments{
        "-hide_banner", "-loglevel", "error", "-nostdin", "-y",  "-i",  proxyEntry_.path,
        "-map",         "0:v:0",     "-an",   "-sn",      "-dn", "-vf", filter};
    if (softwareFallback) {
        arguments << "-c:v"
                  << "libx264"
                  << "-preset"
                  << "veryfast"
                  << "-crf"
                  << "20";
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
                  << "21"
                  << "-b:v"
                  << "0";
    }
    arguments << "-pix_fmt"
              << "yuv420p"
              << "-movflags"
              << "+faststart" << pendingProxyTempPath_;
    encoder->start(executable, arguments);
    Logger::info(QString("Generating %1 video proxy in background: %2")
                     .arg(softwareFallback ? "software" : "hardware", pendingProxyPath_));
}

void WallpaperSurface::handleProxyEncoderFinished(int exitCode, QProcess::ExitStatus status) {
    auto* finishedProcess = proxyEncoder_;
    proxyEncoder_ = nullptr;
    if (finishedProcess)
        finishedProcess->deleteLater();
    if (status != QProcess::NormalExit || exitCode != 0 ||
        QFileInfo(pendingProxyTempPath_).size() <= 0) {
        QFile::remove(pendingProxyTempPath_);
        if (!proxySoftwareFallback_) {
            Logger::warning("Hardware proxy encoding failed; retrying at idle CPU priority");
            startProxyEncoder(true);
        } else {
            Logger::warning("Video proxy encoding failed; keeping original playback");
        }
        return;
    }
    QFile::remove(pendingProxyPath_);
    if (!QFile::rename(pendingProxyTempPath_, pendingProxyPath_)) {
        Logger::warning("Unable to finalize generated video proxy");
        return;
    }
    trimProxyCache(QFileInfo(pendingProxyPath_).absolutePath(), pendingProxyPath_);
    Logger::info("Display-resolution video proxy ready: " + pendingProxyPath_);
    const auto result = startVideoPlayback(pendingProxyPath_, proxyEntry_);
    if (!result.ok)
        Logger::warning("Unable to switch to display-resolution video proxy: " + result.message);
}

Result WallpaperSurface::load(const WallpaperEntry& entry) {
    clearRenderer();
    if (!QFileInfo::exists(entry.path))
        return Result::failure(WallpaperError::FileNotFound, "壁纸源文件已不存在：" + entry.path);

    mode_ = entry.displayMode;
    if (entry.type == WallpaperType::Image) {
        source_.load(entry.path);
        if (source_.isNull())
            return Result::failure(WallpaperError::MediaDecodeFailed,
                                   "图片解码失败：" + entry.path);
        placeImage();
        image_->show();
        return Result::success();
    }
    if (entry.type == WallpaperType::Gif) {
        movie_ = new QMovie(entry.path, {}, this);
        movie_->setCacheMode(QMovie::CacheNone);
        if (!movie_->isValid()) {
            clearRenderer();
            return Result::failure(WallpaperError::MediaDecodeFailed,
                                   "GIF 解码失败：" + entry.path);
        }
        image_->show();
        connect(movie_, &QMovie::frameChanged, this, [this] {
            source_ = movie_->currentPixmap();
            placeImage();
        });
        movie_->start();
        dynamic_ = true;
        return Result::success();
    }
    if (entry.type == WallpaperType::Video) {
        const auto result = startVideoPlayback(entry.path, entry, true);
        if (!result.ok)
            return result;
        dynamic_ = true;
        return Result::success();
    }
    return Result::failure(WallpaperError::UnsupportedFormat, "不支持的壁纸格式");
}

void WallpaperSurface::synchronizeNativeChildren() {
    if (video_)
        video_->updateVideo();
}

void WallpaperSurface::stop() {
    clearRenderer();
}

void WallpaperSurface::pause(bool paused) {
    if (movie_)
        movie_->setPaused(paused);
    if (video_)
        video_->pause(paused);
}

void WallpaperSurface::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    image_->setGeometry(rect());
    if (video_)
        video_->updateVideo();
    placeImage();
}
