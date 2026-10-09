#include "media/MediaInfo.h"
#include "library/WallpaperEntry.h"
#include <QFileInfo>
#include <QImageReader>
Result MediaInspector::inspect(const QString& path, MediaInfo* info) {
    QFileInfo file(path);
    if (!file.exists())
        return Result::failure(WallpaperError::FileNotFound, "文件不存在：" + path);
    if (!info)
        return Result::failure(WallpaperError::Unknown, "输出参数为空");
    info->bytes = file.size();
    if (WallpaperEntry::typeForPath(path) != WallpaperType::Video) {
        QImageReader reader(path);
        info->resolution = reader.size();
        if (!reader.canRead())
            return Result::failure(WallpaperError::MediaDecodeFailed,
                                   "无法读取媒体文件：" + reader.errorString());
    }
    return Result::success();
}
