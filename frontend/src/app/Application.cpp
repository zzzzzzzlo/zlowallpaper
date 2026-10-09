#include "app/Application.h"
#include "common/Logger.h"
#include "config/AppSettings.h"
#include "icontra/IcontraHost.h"
#include "library/WallpaperLibrary.h"
#include "system/ExplorerMonitor.h"
#include "system/MonitorManager.h"
#include "system/PowerEventHandler.h"
#include "system/TranslucentTBHost.h"
#include "ui/MainWindow.h"
#include "wallpaper/WallpaperEngine.h"
#include <QApplication>
#include <QTimer>
#include <windows.h>

namespace {
bool isDesktopShellWindow(HWND window) {
    // Clicking an empty portion of the desktop makes Progman or one of its
    // WorkerW/SHELLDLL_DefView children the foreground window. They occupy a
    // monitor, but are the desktop itself rather than a fullscreen app.
    for (HWND current = window; current; current = GetParent(current)) {
        wchar_t className[64]{};
        GetClassNameW(current, className, 64);
        if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"SHELLDLL_DefView") == 0) {
            return true;
        }
    }
    return false;
}

bool foregroundWindowCoversMonitor(qint64 managedDockProcessId) {
    const HWND foreground = GetForegroundWindow();
    if (!foreground || !IsWindowVisible(foreground) || IsIconic(foreground))
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    // Icontra is a separately managed Electron process. Its transparent Dock
    // window can become the foreground window after Win+D, but it is part of
    // Asterol rather than a fullscreen application that should hide itself.
    if (processId == GetCurrentProcessId() ||
        (managedDockProcessId > 0 && processId == static_cast<DWORD>(managedDockProcessId)))
        return false;
    if (isDesktopShellWindow(foreground))
        return false;

    RECT windowRect{};
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    const auto monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    if (!GetWindowRect(foreground, &windowRect) || !monitor ||
        !GetMonitorInfoW(monitor, &monitorInfo))
        return false;

    // A normal maximized window ends at the monitor work area above the taskbar.
    // Only pause when the window really covers the physical monitor bounds.
    constexpr LONG EdgeTolerancePx = 8;
    const auto& monitorRect = monitorInfo.rcMonitor;
    return windowRect.left <= monitorRect.left + EdgeTolerancePx &&
           windowRect.top <= monitorRect.top + EdgeTolerancePx &&
           windowRect.right >= monitorRect.right - EdgeTolerancePx &&
           windowRect.bottom >= monitorRect.bottom - EdgeTolerancePx;
}
} // namespace

Application::Application(QObject* parent)
    : QObject(parent), library_(new WallpaperLibrary(this)), monitors_(new MonitorManager(this)),
      engine_(new WallpaperEngine(monitors_, this)), explorer_(new ExplorerMonitor(this)),
      power_(new PowerEventHandler(this)), icontra_(new IcontraHost(this)),
      translucentTb_(new TranslucentTBHost(this)), activityTimer_(new QTimer(this)) {}

bool Application::start() {
    Logger::initialize(AppSettings().loggingEnabled() ||
                       qApp->arguments().contains("--icontra-smoke"));
    Logger::info("Application started; Qt=" + QString(qVersion()));
    library_->load();
    window_ = new MainWindow(library_, engine_, monitors_, icontra_, translucentTb_);
    Logger::info(QString("Wallpaper library loaded=%1, list displayed=%2")
                     .arg(library_->entries().size())
                     .arg(window_->displayedEntryCount()));
    explorer_->start();
    qApp->installNativeEventFilter(power_);
    connect(explorer_, &ExplorerMonitor::desktopMayHaveChanged, engine_,
            &WallpaperEngine::recoverDesktop);
    connect(explorer_, &ExplorerMonitor::desktopMayHaveChanged, icontra_,
            &IcontraHost::recoverAfterExplorer);
    connect(power_, &PowerEventHandler::suspendRequested, engine_, [this] {
        engine_->suspend();
        icontra_->suspend();
        Logger::info("Wallpaper suspended for system sleep");
    });
    connect(power_, &PowerEventHandler::resumeRequested, engine_, [this] {
        engine_->recoverDesktop();
        icontra_->resume();
        Logger::info("Wallpaper recovery scheduled after system resume");
    });

    activityTimer_->setInterval(1000);
    connect(activityTimer_, &QTimer::timeout, this, [this] {
        const bool shouldPause =
            AppSettings().pauseWhenInactive() &&
            foregroundWindowCoversMonitor(icontra_ ? icontra_->processId() : 0);
        if (shouldPause == autoPaused_)
            return;
        autoPaused_ = shouldPause;
        icontra_->setFullscreenActive(autoPaused_);
        syncPauseState();
    });
    connect(icontra_, &IcontraHost::fullscreenChanged, this, [this](bool active) {
        dockFullscreen_ = active;
        syncPauseState();
    });
    activityTimer_->start();

    window_->show();
    const auto id = AppSettings().activeWallpaperId();
    if (!qApp->arguments().contains("--ui-smoke") && !qApp->arguments().contains("--icontra-smoke") && AppSettings().restoreLastWallpaper() &&
        !id.isEmpty()) {
        if (const auto* entry = library_->find(id)) {
            const auto result = engine_->apply(*entry);
            if (!result.ok)
                Logger::warning("Unable to restore wallpaper: " + result.message);
        }
    }
    // Creating Electron, its GPU process and its first icon state is not
    // needed for the wallpaper window to become usable.  Let Qt finish the
    // initial paint and media restore first, then start the managed Dock.
    const int dockStartDelay = qApp->arguments().contains("--icontra-smoke") ? 0 : 1500;
    QTimer::singleShot(dockStartDelay, this, [this] {
        if (qApp->arguments().contains("--ui-smoke")) return;
        icontra_->start();
        translucentTb_->startIfEnabled();
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        Logger::info("Application exiting");
        engine_->stop();
        translucentTb_->shutdown();
        icontra_->shutdown();
        library_->save();
    });
    return true;
}

void Application::syncPauseState() {
    engine_->pause(autoPaused_ || dockFullscreen_);
}

void Application::startSmokeWallpaper() {
    if (library_->entries().isEmpty()) {
        Logger::error("Smoke test requested but wallpaper library is empty");
        return;
    }
    window_->hide();
    const auto result = engine_->apply(library_->entries().front());
    if (result.ok)
        Logger::info("Smoke wallpaper attached successfully");
    else
        Logger::error("Smoke wallpaper failed: " + result.message + " debug=" + result.debug);
}

void Application::recoverSmokeWallpaper() {
    engine_->recoverDesktop();
}

void Application::startIcontraSmoke() {
    window_->hide();
}
