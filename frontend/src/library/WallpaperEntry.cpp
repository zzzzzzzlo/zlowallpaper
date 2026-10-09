#include "library/WallpaperEntry.h"
#include <QFileInfo>
#include <QStringList>
#include <QUuid>

WallpaperType WallpaperEntry::typeForPath(const QString& path) {
    const auto ext = QFileInfo(path).suffix().toLower();
    if (QStringList{"jpg", "jpeg", "png", "bmp", "webp"}.contains(ext))
        return WallpaperType::Image;
    if (ext == "gif")
        return WallpaperType::Gif;
    if (QStringList{"mp4", "webm", "mov", "avi"}.contains(ext))
        return WallpaperType::Video;
    return WallpaperType::Unknown;
}
QString WallpaperEntry::typeName(WallpaperType t) {
    switch (t) {
    case WallpaperType::Image:
        return "图片";
    case WallpaperType::Gif:
        return "GIF";
    case WallpaperType::Video:
        return "视频";
    default:
        return "未知";
    }
}
QString WallpaperEntry::displayModeName(DisplayMode m) {
    switch (m) {
    case DisplayMode::Fill:
        return "填充";
    case DisplayMode::Fit:
        return "适应";
    case DisplayMode::Stretch:
        return "拉伸";
    case DisplayMode::Center:
        return "居中";
    case DisplayMode::Tile:
        return "平铺";
    }
    return "填充";
}
QJsonObject WallpaperEntry::toJson() const {
    return {{"id", id},
            {"path", path},
            {"name", fileName},
            {"mime", mimeType},
            {"type", int(type)},
            {"thumb", thumbnailPath},
            {"storeProductId", storeProductId},
            {"storeResourceVersion", storeResourceVersion},
            {"storeAccountKey", storeAccountKey},
            {"thumbVersion", thumbnailVersion},
            {"added", addedAt.toString(Qt::ISODate)},
            {"used", lastUsedAt.toString(Qt::ISODate)},
            {"position", QString::number(lastPositionMs)},
            {"mode", int(displayMode)},
            {"muted", muted}};
}
WallpaperEntry WallpaperEntry::fromJson(const QJsonObject& j) {
    WallpaperEntry e;
    e.id = j["id"].toString();
    e.path = j["path"].toString();
    e.fileName = j["name"].toString();
    e.mimeType = j["mime"].toString();
    e.type = WallpaperType(j["type"].toInt());
    e.thumbnailPath = j["thumb"].toString();
    e.storeProductId = j["storeProductId"].toString();
    e.storeResourceVersion = j["storeResourceVersion"].toString();
    e.storeAccountKey = j["storeAccountKey"].toString();
    e.thumbnailVersion = j["thumbVersion"].toInt();
    e.addedAt = QDateTime::fromString(j["added"].toString(), Qt::ISODate);
    e.lastUsedAt = QDateTime::fromString(j["used"].toString(), Qt::ISODate);
    e.lastPositionMs = j["position"].toString().toLongLong();
    e.displayMode = DisplayMode(j["mode"].toInt());
    e.muted = j["muted"].toBool(true);
    e.valid = QFileInfo::exists(e.path);
    if (e.id.isEmpty())
        e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return e;
}
