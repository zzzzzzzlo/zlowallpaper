#pragma once
#include <QString>

enum class WallpaperError {
    None,
    FileNotFound,
    UnsupportedFormat,
    MediaDecodeFailed,
    DesktopWindowNotFound,
    ShellViewNotFound,
    WorkerWNotFound,
    SetParentFailed,
    WindowStyleFailed,
    RendererInitializationFailed,
    PlaybackFailed,
    InvalidMonitor,
    ExplorerRestarted,
    Unknown
};

struct Result {
    bool ok = false;
    WallpaperError error = WallpaperError::Unknown;
    QString message;
    unsigned long win32Error = 0;
    QString debug;
    static Result success(const QString& message = {}) {
        return {true, WallpaperError::None, message, 0, {}};
    }
    static Result failure(WallpaperError error, const QString& message, unsigned long code = 0,
                          const QString& debug = {}) {
        return {false, error, message, code, debug};
    }
};
