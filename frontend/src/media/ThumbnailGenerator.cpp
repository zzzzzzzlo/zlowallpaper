#include "media/ThumbnailGenerator.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <windows.h>

namespace {
constexpr QSize ThumbnailSize(320, 180);

QImage fallbackImage(const WallpaperEntry& entry) {
    QImage image(ThumbnailSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#263238"));
    QPainter painter(&image);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter,
                     entry.type == WallpaperType::Video ? "VIDEO" : "无法预览");
    return image;
}

bool extractVideoFrame(const QString& source, const QString& output) {
    const auto executable = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (!QFileInfo::exists(executable))
        return false;

    QProcess process;
    process.start(executable,
                  {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-ss", "00:00:01", "-i",
                   source, "-frames:v", "1", "-vf",
                   "scale=320:180:force_original_aspect_ratio=increase,crop=320:180", output});
    if (!process.waitForStarted(1500))
        return false;
    if (const auto handle =
            OpenProcess(PROCESS_SET_INFORMATION, FALSE, DWORD(process.processId()))) {
        SetPriorityClass(handle, IDLE_PRIORITY_CLASS);
        CloseHandle(handle);
    }
    if (!process.waitForFinished(6000)) {
        process.kill();
        process.waitForFinished(500);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
           QFileInfo(output).size() > 0;
}
} // namespace

QString ThumbnailGenerator::create(const WallpaperEntry& entry) {
    const auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/thumbnails";
    QDir().mkpath(directory);
    const auto output = directory + "/" + entry.id + ".png";

    if (entry.type == WallpaperType::Video && extractVideoFrame(entry.path, output))
        return output;

    QImage image;
    if (entry.type != WallpaperType::Video) {
        QImageReader reader(entry.path);
        reader.setAutoTransform(true);
        const auto sourceSize = reader.size();
        if (sourceSize.isValid())
            reader.setScaledSize(sourceSize.scaled(ThumbnailSize, Qt::KeepAspectRatioByExpanding));
        image = reader.read();
    }
    if (image.isNull())
        image = fallbackImage(entry);
    image.scaled(ThumbnailSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
        .save(output);
    return output;
}
