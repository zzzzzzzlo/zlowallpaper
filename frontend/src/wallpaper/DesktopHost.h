#pragma once
#include "common/Result.h"
#include <QRect>
#include <windows.h>

class DesktopHost final {
  public:
    Result attach(HWND wallpaper, const QRect& geometry);
    // During a shell transition the WorkerW owner can be rebuilding on
    // Explorer's UI thread. Reparenting out of it is then a synchronous
    // cross-process operation and can deadlock both processes. Callers that
    // are tearing down for recovery can safely skip that optional reparent.
    void detach(HWND wallpaper, bool restoreTopLevel = true);
    bool isAttached(HWND wallpaper) const;
    HWND workerW() const {
        return workerW_;
    }

  private:
    Result findWorkerW();
    HWND workerW_ = nullptr;
};
