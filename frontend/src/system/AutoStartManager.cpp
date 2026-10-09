#include "system/AutoStartManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {
constexpr auto RunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto Name = "ZloWallpaper";
QSettings reg(RunKey, QSettings::NativeFormat);

QString currentCommand() {
    return "\"" + QCoreApplication::applicationFilePath() + "\"";
}

void migrateLegacyEntry() {
    // Independent application: never touch Asterol startup entries.
}
} // namespace

bool AutoStartManager::isEnabled() {
    migrateLegacyEntry();
    return reg.value(Name).toString() == currentCommand();
}

Result AutoStartManager::setEnabled(bool enabled) {
    migrateLegacyEntry();
    if (enabled) {
        const auto value = currentCommand();
        reg.setValue(Name, value);
        reg.sync();
        if (reg.status() != QSettings::NoError || reg.value(Name).toString() != value)
            return Result::failure(WallpaperError::Unknown, "写入当前用户启动项失败");
    } else {
        reg.remove(Name);
        reg.sync();
        if (reg.status() != QSettings::NoError || reg.contains(Name))
            return Result::failure(WallpaperError::Unknown, "删除当前用户启动项失败");
    }
    return Result::success();
}
