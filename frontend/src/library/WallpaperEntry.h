#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QString>

enum class WallpaperType { Image, Gif, Video, Unknown };
enum class DisplayMode { Fill, Fit, Stretch, Center, Tile };

struct WallpaperEntry {
    QString id;
    QString path;
    QString fileName;
    QString mimeType;
    WallpaperType type = WallpaperType::Unknown;
    QString thumbnailPath;
    QString storeProductId;
    QString storeResourceVersion;
    QString storeAccountKey;
    int thumbnailVersion = 0;
    QDateTime addedAt;
    QDateTime lastUsedAt;
    qint64 lastPositionMs = 0;
    DisplayMode displayMode = DisplayMode::Fill;
    bool muted = true;
    bool valid = false;
    QJsonObject toJson() const;
    static WallpaperEntry fromJson(const QJsonObject& json);
    static WallpaperType typeForPath(const QString& path);
    static QString typeName(WallpaperType type);
    static QString displayModeName(DisplayMode mode);
};
