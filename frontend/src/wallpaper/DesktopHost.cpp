#include "wallpaper/DesktopHost.h"
#include "common/Logger.h"
#include <QVector>
#include <array>

namespace {
constexpr UINT SpawnWorkerW = 0x052C;
constexpr LONG_PTR ExNoRedirectionBitmap = 0x00200000L;
struct Search {
    HWND worker = nullptr;
    HWND iconHost = nullptr;
    bool sawShellView = false;
    QVector<QString> windows;
};

QString windowClass(HWND window) {
    wchar_t name[128]{};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return QString::fromWCharArray(name);
}

BOOL CALLBACK enumDesktopWindows(HWND top, LPARAM data) {
    auto* search = reinterpret_cast<Search*>(data);
    const auto className = windowClass(top);
    search->windows.append(
        QString("%1=%2").arg(className).arg(reinterpret_cast<quintptr>(top), 0, 16));
    if (const HWND shellView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
        search->sawShellView = true;
        search->iconHost = top;
        // The usual Explorer layout has a second WorkerW immediately behind the
        // window which owns SHELLDLL_DefView.
        search->worker = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        return search->worker ? FALSE : TRUE;
    }
    return TRUE;
}
} // namespace

Result DesktopHost::findWorkerW() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman)
        return Result::failure(WallpaperError::DesktopWindowNotFound, "未找到 Progman 桌面宿主",
                               GetLastError());
    if (IsHungAppWindow(progman))
        return Result::failure(WallpaperError::DesktopWindowNotFound,
                               "Explorer 正在恢复桌面，请稍后重试");
    Logger::info(QString("Progman=%1").arg(reinterpret_cast<quintptr>(progman), 0, 16));

    // Explorer 10/11 recognizes 0xD/0x1. The message creates the wallpaper
    // WorkerW if it has not yet been created for this desktop session.
    DWORD_PTR ignored = 0;
    const auto spawned = SendMessageTimeoutW(progman, SpawnWorkerW, 0xD, 0x1,
                                             SMTO_NORMAL | SMTO_ABORTIFHUNG, 1500, &ignored);
    if (!spawned) {
        Logger::warning(QString("WorkerW spawn message timed out: %1").arg(GetLastError()));
        SendMessageTimeoutW(progman, SpawnWorkerW, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, 1500,
                            &ignored);
    }

    const bool raisedDesktop =
        (GetWindowLongPtrW(progman, GWL_EXSTYLE) & ExNoRedirectionBitmap) != 0;
    Search search;
    if (raisedDesktop) {
        // Windows 11 raised desktop: DefView remains a child of Progman and
        // the dedicated wallpaper WorkerW is also a *child* of Progman. It is
        // not discoverable via EnumWindows, which only visits top-level HWNDs.
        for (int attempt = 0; attempt != 5 && !search.worker; ++attempt) {
            search.worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
            if (!search.worker)
                Sleep(30);
        }
        search.sawShellView =
            FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
        Logger::info(QString("Raised desktop: child WorkerW=%1")
                         .arg(reinterpret_cast<quintptr>(search.worker), 0, 16));
    } else {
        EnumWindows(enumDesktopWindows, reinterpret_cast<LPARAM>(&search));
        if (!search.worker && search.iconHost && windowClass(search.iconHost) == "WorkerW") {
            search.worker = search.iconHost;
            Logger::warning("Explorer uses a single top-level WorkerW desktop host");
        }
    }
    if (!search.sawShellView)
        return Result::failure(WallpaperError::ShellViewNotFound,
                               "未找到 SHELLDLL_DefView；Explorer 桌面尚不可用");
    if (!search.worker) {
        const auto diagnostic = search.windows.join(", ");
        Logger::error("No usable WorkerW. Top-level desktop candidates: " + diagnostic);
        return Result::failure(WallpaperError::WorkerWNotFound,
                               "未找到可用的 WorkerW；Explorer 当前桌面布局不支持安全附着", 0,
                               diagnostic);
    }
    workerW_ = search.worker;
    if (raisedDesktop) {
        // A raised desktop requires the WorkerW to remain below the layered
        // ShellView, otherwise desktop icons are visually covered.
        if (!SetWindowPos(workerW_, HWND_BOTTOM, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
            return Result::failure(WallpaperError::WindowStyleFailed,
                                   "无法校正 WorkerW 与桌面图标的层级", GetLastError());
        }
    }
    Logger::info(QString("Desktop host=%1 class=%2")
                     .arg(reinterpret_cast<quintptr>(workerW_), 0, 16)
                     .arg(windowClass(workerW_)));
    return Result::success();
}

Result DesktopHost::attach(HWND wallpaper, const QRect& geometry) {
    if (!wallpaper || !IsWindow(wallpaper))
        return Result::failure(WallpaperError::RendererInitializationFailed, "壁纸渲染窗口无效");
    auto result = findWorkerW();
    if (!result.ok)
        return result;
    const auto oldStyle = GetWindowLongPtrW(wallpaper, GWL_STYLE);
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(wallpaper, GWL_STYLE,
                      (oldStyle | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN) & ~WS_POPUP);
    const DWORD styleError = GetLastError();
    if (styleError != ERROR_SUCCESS)
        return Result::failure(WallpaperError::WindowStyleFailed, "设置壁纸窗口样式失败",
                               styleError);
    const auto oldEx = GetWindowLongPtrW(wallpaper, GWL_EXSTYLE);
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(wallpaper, GWL_EXSTYLE,
                      oldEx | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED);
    if (GetLastError() != ERROR_SUCCESS)
        return Result::failure(WallpaperError::WindowStyleFailed, "设置壁纸扩展窗口样式失败",
                               GetLastError());
    if (!SetLayeredWindowAttributes(wallpaper, 0, 0xFF, LWA_ALPHA))
        return Result::failure(WallpaperError::WindowStyleFailed, "启用分层桌面渲染失败",
                               GetLastError());
    SetLastError(ERROR_SUCCESS);
    const HWND previous = SetParent(wallpaper, workerW_);
    if (!previous && GetLastError() != ERROR_SUCCESS)
        return Result::failure(WallpaperError::SetParentFailed,
                               "SetParent 未能将壁纸窗口附着到 WorkerW", GetLastError());
    if (GetParent(wallpaper) != workerW_)
        return Result::failure(WallpaperError::SetParentFailed, "SetParent 后父窗口校验失败");
    // Child coordinates are relative to WorkerW, not virtual-screen
    // coordinates. In particular, virtual-screen geometry and Qt logical DPI
    // geometry diverge on a scaled or multi-monitor desktop.
    RECT parentClient{};
    if (!GetClientRect(workerW_, &parentClient))
        return Result::failure(WallpaperError::InvalidMonitor, "无法读取 WorkerW 客户区尺寸",
                               GetLastError());
    const int targetWidth = parentClient.right - parentClient.left;
    const int targetHeight = parentClient.bottom - parentClient.top;
    if (targetWidth <= 0 || targetHeight <= 0)
        return Result::failure(WallpaperError::InvalidMonitor, "WorkerW 客户区尺寸异常");
    if (!SetWindowPos(wallpaper, HWND_BOTTOM, 0, 0, targetWidth, targetHeight,
                      SWP_NOACTIVATE | SWP_SHOWWINDOW))
        return Result::failure(WallpaperError::WindowStyleFailed, "调整壁纸窗口尺寸失败",
                               GetLastError());
    RECT rect{};
    GetWindowRect(wallpaper, &rect);
    const QRect actual(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    if (!IsWindowVisible(wallpaper))
        return Result::failure(WallpaperError::RendererInitializationFailed, "壁纸窗口不可见");
    if (actual.width() < targetWidth - 2 || actual.height() < targetHeight - 2)
        return Result::failure(WallpaperError::InvalidMonitor, "壁纸窗口尺寸未覆盖目标桌面区域", 0,
                               QString("actual=%1x%2 target=%3x%4")
                                   .arg(actual.width())
                                   .arg(actual.height())
                                   .arg(targetWidth)
                                   .arg(targetHeight));
    return Result::success("桌面宿主、父窗口、可见性和尺寸校验均通过");
}
void DesktopHost::detach(HWND wallpaper, bool restoreTopLevel) {
    const bool attached = wallpaper && IsWindow(wallpaper) && GetParent(wallpaper) == workerW_;
    if (attached && restoreTopLevel) {
        if (!SetParent(wallpaper, nullptr))
            Logger::warning(
                QString("Unable to detach wallpaper from WorkerW: %1").arg(GetLastError()));
    } else if (attached) {
        Logger::info("Skipping WorkerW reparent during desktop recovery");
    }
    workerW_ = nullptr;
}
bool DesktopHost::isAttached(HWND wallpaper) const {
    return workerW_ && IsWindow(workerW_) && wallpaper && IsWindow(wallpaper) &&
           GetParent(wallpaper) == workerW_;
}
