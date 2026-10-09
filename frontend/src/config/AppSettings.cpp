#include "config/AppSettings.h"
#include <QSettings>

namespace {
QSettings& settings() {
    static QSettings current("ZloWallpaper", "ZloWallpaper");
    return current;
}
} // namespace

bool AppSettings::restoreLastWallpaper() const {
    return settings().value("behavior/restore", true).toBool();
}
void AppSettings::setRestoreLastWallpaper(bool value) {
    settings().setValue("behavior/restore", value);
}
bool AppSettings::minimizeToTray() const {
    return settings().value("behavior/minimizeToTray", true).toBool();
}
void AppSettings::setMinimizeToTray(bool value) {
    settings().setValue("behavior/minimizeToTray", value);
}
bool AppSettings::muteVideos() const {
    return settings().value("media/mute", true).toBool();
}
void AppSettings::setMuteVideos(bool value) {
    settings().setValue("media/mute", value);
}
bool AppSettings::pauseWhenInactive() const {
    return settings().value("behavior/pauseWhenInactive", true).toBool();
}
void AppSettings::setPauseWhenInactive(bool value) {
    settings().setValue("behavior/pauseWhenInactive", value);
}
bool AppSettings::loggingEnabled() const {
    return settings().value("advanced/logging", true).toBool();
}
void AppSettings::setLoggingEnabled(bool value) {
    settings().setValue("advanced/logging", value);
}
DisplayMode AppSettings::displayMode() const {
    return DisplayMode(settings().value("media/displayMode", int(DisplayMode::Fill)).toInt());
}
void AppSettings::setDisplayMode(DisplayMode value) {
    settings().setValue("media/displayMode", int(value));
}
QString AppSettings::activeWallpaperId() const {
    return settings().value("runtime/activeId").toString();
}
void AppSettings::setActiveWallpaperId(const QString& id) {
    settings().setValue("runtime/activeId", id);
}
QString AppSettings::originalSystemWallpaper() const {
    return settings().value("runtime/originalSystemWallpaper").toString();
}
void AppSettings::setOriginalSystemWallpaper(const QString& path) {
    settings().setValue("runtime/originalSystemWallpaper", path);
}
QByteArray AppSettings::windowGeometry() const {
    return settings().value("ui/geometry").toByteArray();
}
void AppSettings::setWindowGeometry(const QByteArray& value) {
    settings().setValue("ui/geometry", value);
}
bool AppSettings::icontraEnabled() const {
    return settings().value("icontra/enabled", true).toBool();
}
void AppSettings::setIcontraEnabled(bool value) {
    settings().setValue("icontra/enabled", value);
}
bool AppSettings::translucentTbEnabled() const {
    return settings().value("desktop/translucentTaskbar", false).toBool();
}
void AppSettings::setTranslucentTbEnabled(bool value) {
    settings().setValue("desktop/translucentTaskbar", value);
}
