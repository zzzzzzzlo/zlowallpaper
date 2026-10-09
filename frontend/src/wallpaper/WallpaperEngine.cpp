#include "wallpaper/WallpaperEngine.h"
#include "common/Logger.h"
#include "config/AppSettings.h"
#include "system/MonitorManager.h"
#include "wallpaper/DesktopHost.h"
#include "wallpaper/WallpaperSurface.h"
#include <QTimer>
#include <array>
#include <windows.h>

namespace {
// Modern Standby can resume application timers before Explorer and the GPU
// driver have finished rebuilding their desktop surfaces. A conservative
// delay avoids attaching a renderer to a WorkerW in that transition window.
constexpr int ResumeRecoveryDelayMs = 8000;
constexpr std::array<int, 5> RetryRecoveryDelaysMs{1500, 3000, 5000, 8000, 12000};
constexpr int SlowRecoveryDelayMs = 15000;
constexpr int PlaybackStabilityWindowMs = 30000;
constexpr std::array<int, 3> PlaybackRecoveryDelaysMs{1000, 3000, 8000};
} // namespace

WallpaperEngine::WallpaperEngine(MonitorManager* monitors, QObject* parent)
    : QObject(parent), host_(new DesktopHost), monitors_(monitors),
      recoveryTimer_(new QTimer(this)), playbackStabilityTimer_(new QTimer(this)) {
    recoveryTimer_->setSingleShot(true);
    connect(recoveryTimer_, &QTimer::timeout, this, &WallpaperEngine::rebuildAfterDesktopChange);
    playbackStabilityTimer_->setSingleShot(true);
    connect(playbackStabilityTimer_, &QTimer::timeout, this,
            [this] { playbackErrorRecoveryAttempts_ = 0; });
    connect(monitors_, &MonitorManager::topologyChanged, this, &WallpaperEngine::recoverDesktop);
    createSurface();
}

WallpaperEngine::~WallpaperEngine() {
    disposeSurface();
    delete host_;
}

void WallpaperEngine::createSurface() {
    if (surface_)
        return;
    surface_ = new WallpaperSurface;
    connect(surface_, &WallpaperSurface::playbackError, this,
            &WallpaperEngine::handlePlaybackError);
}

void WallpaperEngine::disposeSurface(bool restoreTopLevel) {
    if (!surface_)
        return;

    const auto wallpaper = reinterpret_cast<HWND>(surface_->winId());
    Logger::info(restoreTopLevel ? "Releasing wallpaper renderer"
                                 : "Releasing wallpaper renderer for desktop recovery");
    surface_->stop();
    host_->detach(wallpaper, restoreTopLevel);
    surface_->hide();
    delete surface_;
    surface_ = nullptr;
}

void WallpaperEngine::rememberSystemWallpaper() {
    AppSettings settings;
    if (!settings.originalSystemWallpaper().isEmpty())
        return;
    wchar_t path[MAX_PATH]{};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0))
        settings.setOriginalSystemWallpaper(QString::fromWCharArray(path));
}

Result WallpaperEngine::attachAndLoad(const WallpaperEntry& entry) {
    createSurface();
    surface_->resize(monitors_->virtualGeometry().size());
    surface_->show();

    auto result =
        host_->attach(reinterpret_cast<HWND>(surface_->winId()), monitors_->virtualGeometry());
    if (!result.ok) {
        surface_->hide();
        return result;
    }

    result = surface_->load(entry);
    if (!result.ok) {
        host_->detach(reinterpret_cast<HWND>(surface_->winId()));
        surface_->stop();
        surface_->hide();
        return result;
    }
    surface_->synchronizeNativeChildren();
    return Result::success();
}

Result WallpaperEngine::apply(const WallpaperEntry& entry) {
    if (!entry.valid)
        return Result::failure(WallpaperError::FileNotFound, "文件不存在，无法设置为壁纸");

    rememberSystemWallpaper();
    stop();

    // Static images are more reliable when Windows owns them directly. They
    // do not need an embedded renderer, and consequently keep working across
    // Explorer restarts and display-device sleep/resume transitions.
    if (entry.type == WallpaperType::Image) {
        const auto path = entry.path.toStdWString();
        if (path.empty() ||
            !SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(path.c_str()),
                                   SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) {
            return Result::failure(
                WallpaperError::WindowStyleFailed,
                "Windows could not apply the selected image as the system wallpaper",
                GetLastError());
        }
        Logger::info("Static image applied through Windows wallpaper settings: " + entry.path);
        return Result::success();
    }

    activeEntry_ = entry;
    activeId_ = entry.id;
    active_ = true;
    suspended_ = false;
    playbackErrorRecoveryAttempts_ = 0;

    const auto result = attachAndLoad(activeEntry_);
    if (!result.ok) {
        disposeSurface();
        active_ = false;
        activeId_.clear();
        activeEntry_ = {};
        return result;
    }
    Logger::info("Wallpaper attached: " + entry.path);
    armPlaybackStabilityReset();
    return Result::success();
}

void WallpaperEngine::stop() {
    recoveryTimer_->stop();
    playbackStabilityTimer_->stop();
    playbackErrorRecoveryAttempts_ = 0;
    disposeSurface();
    suspended_ = false;
    if (active_) {
        active_ = false;
        activeId_.clear();
        activeEntry_ = {};
        emit stopped();
    }
    Logger::info("Wallpaper stopped");
}

void WallpaperEngine::pause(bool paused) {
    if (active_ && surface_)
        surface_->pause(paused);
}

void WallpaperEngine::suspend() {
    if (!active_)
        return;
    suspended_ = true;
    recoveryTimer_->stop();
    playbackStabilityTimer_->stop();
    playbackErrorRecoveryAttempts_ = 0;

    // The old renderer belongs to this process, so it can be destroyed
    // directly. Avoid the optional cross-process WorkerW reparent while the
    // system is transitioning to sleep.
    disposeSurface(false);
    Logger::info("Wallpaper renderer released for system suspend");
}

void WallpaperEngine::recoverDesktop() {
    if (!active_)
        return;
    if (recoveryTimer_->isActive()) {
        Logger::info("Desktop recovery request coalesced with an existing retry");
        return;
    }
    suspended_ = false;
    recoveryAttempts_ = 0;
    playbackErrorRecoveryAttempts_ = 0;
    scheduleRecovery(ResumeRecoveryDelayMs);
}

void WallpaperEngine::scheduleRecovery(int delayMs) {
    if (!active_)
        return;
    recoveryTimer_->start(delayMs);
}

void WallpaperEngine::rebuildAfterDesktopChange() {
    if (!active_ || suspended_)
        return;

    // A resumed Explorer may have destroyed WorkerW and invalidated the old
    // child HWND. Recreate both the native media renderer and its Qt host.
    // Do not synchronously reparent the old child through Explorer's UI
    // thread, because it can still be blocked during Modern Standby resume.
    disposeSurface(false);
    const auto result = attachAndLoad(activeEntry_);
    if (result.ok) {
        recoveryAttempts_ = 0;
        armPlaybackStabilityReset();
        Logger::info("Wallpaper renderer recreated after desktop change");
        return;
    }

    disposeSurface();
    ++recoveryAttempts_;
    const auto retryDelay = recoveryAttempts_ <= static_cast<int>(RetryRecoveryDelaysMs.size())
                                ? RetryRecoveryDelaysMs.at(recoveryAttempts_ - 1)
                                : SlowRecoveryDelayMs;
    Logger::warning(
        QString("Wallpaper recovery attempt %1 failed: %2 (Win32=%3); retrying in %4 ms")
            .arg(recoveryAttempts_)
            .arg(result.message)
            .arg(result.win32Error)
            .arg(retryDelay));
    scheduleRecovery(retryDelay);
    return;

    active_ = false;
    emit stopped();
    emit failed(Result::failure(WallpaperError::ExplorerRestarted,
                                "壁纸播放器或桌面恢复失败，请在 Explorer 完全就绪后重新设置壁纸。",
                                result.win32Error, result.debug));
}

void WallpaperEngine::handlePlaybackError(const QString& message) {
    Logger::warning("Wallpaper playback error; scheduling renderer recovery: " + message);
    if (!active_ || suspended_ || recoveryTimer_->isActive())
        return;
    playbackStabilityTimer_->stop();
    if (playbackErrorRecoveryAttempts_ >= static_cast<int>(PlaybackRecoveryDelaysMs.size())) {
        Logger::error("Wallpaper playback recovery circuit breaker opened");
        disposeSurface();
        active_ = false;
        activeId_.clear();
        activeEntry_ = {};
        emit stopped();
        emit failed(Result::failure(
            WallpaperError::PlaybackFailed,
            "壁纸连续播放失败，已停止自动重试以保护系统响应。请检查媒体文件后重新设置壁纸。"));
        return;
    }
    const auto delayMs = PlaybackRecoveryDelaysMs.at(playbackErrorRecoveryAttempts_++);
    scheduleRecovery(delayMs);
}

void WallpaperEngine::armPlaybackStabilityReset() {
    if (active_ && !suspended_)
        playbackStabilityTimer_->start(PlaybackStabilityWindowMs);
}
