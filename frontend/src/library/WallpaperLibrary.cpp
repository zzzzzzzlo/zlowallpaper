#include "library/WallpaperLibrary.h"
#include "common/Result.h"
#include "media/ThumbnailGenerator.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include "common/AtomicFile.h"
#include <QStandardPaths>
#include <QUuid>

WallpaperLibrary::WallpaperLibrary(QObject* parent, const QString& storageDirectory)
    : QObject(parent), storageDirectory_(storageDirectory) {}
QString WallpaperLibrary::filePath() const {
    const auto d = storageDirectory_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : storageDirectory_;
    QDir().mkpath(d);
    return d + "/library.json";
}
void WallpaperLibrary::load() {
    entries_.clear();
    QFile f(filePath());
    if (f.open(QIODevice::ReadOnly)) {
        const auto a = QJsonDocument::fromJson(f.readAll()).array();
        for (const auto& v : a)
            entries_ << WallpaperEntry::fromJson(v.toObject());
    }
    bool upgraded = false;
    for (auto& entry : entries_)
        if (entry.valid && entry.type == WallpaperType::Video && entry.thumbnailVersion < 2) {
            entry.thumbnailPath = ThumbnailGenerator::create(entry);
            entry.thumbnailVersion = 2;
            upgraded = true;
        }
    if (upgraded)
        save();
    refreshValidity();
}
bool WallpaperLibrary::save(QString* error) const {
    QJsonArray a;
    for (const auto& e : entries_)
        a.append(e.toJson());
    AtomicFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const auto data = QJsonDocument(a).toJson();
    if (f.write(data) != data.size()) {
        if (error)
            *error = f.errorString();
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
WallpaperEntry* WallpaperLibrary::find(const QString& id) {
    for (auto& e : entries_)
        if (e.id == id)
            return &e;
    return nullptr;
}
const WallpaperEntry* WallpaperLibrary::find(const QString& id) const {
    for (const auto& e : entries_)
        if (e.id == id)
            return &e;
    return nullptr;
}
Result WallpaperLibrary::add(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return Result::failure(WallpaperError::FileNotFound, "文件不存在：" + path);
    const auto type = WallpaperEntry::typeForPath(path);
    if (type == WallpaperType::Unknown)
        return Result::failure(WallpaperError::UnsupportedFormat,
                               "不支持的文件格式：" + fi.suffix());
    for (const auto& e : entries_)
        if (QFileInfo(e.path).canonicalFilePath() == fi.canonicalFilePath())
            return Result::success("壁纸已在库中");
    WallpaperEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.path = fi.absoluteFilePath();
    e.fileName = fi.fileName();
    e.type = type;
    e.mimeType = QMimeDatabase().mimeTypeForFile(fi).name();
    e.addedAt = QDateTime::currentDateTime();
    e.valid = true;
    e.thumbnailPath = ThumbnailGenerator::create(e);
    e.thumbnailVersion = 2;
    return addPrepared(e);
}
const WallpaperEntry* WallpaperLibrary::findStoreEntry(const QString& productId, const QString& version,
                                                       const QString& accountKey) const {
    for (const auto& entry : entries_)
        if (entry.storeProductId == productId && entry.storeResourceVersion == version &&
            entry.storeAccountKey == accountKey && QFileInfo::exists(entry.path)) return &entry;
    return nullptr;
}
Result WallpaperLibrary::addPrepared(const WallpaperEntry& entry) {
    if (!QFileInfo::exists(entry.path))
        return Result::failure(WallpaperError::FileNotFound, "下载文件不存在");
    if (!entry.storeProductId.isEmpty() && findStoreEntry(entry.storeProductId, entry.storeResourceVersion, entry.storeAccountKey))
        return Result::success("该版本已在本地库中");
    entries_ << entry;
    QString error;
    if (!save(&error)) {
        entries_.removeLast();
        return Result::failure(WallpaperError::Unknown, "壁纸已下载，但保存资料库失败：" + error);
    }
    emit changed();
    return Result::success("已加入本地库");
}
bool WallpaperLibrary::remove(const QString& id) {
    for (int i = 0; i < entries_.size(); ++i)
        if (entries_[i].id == id) {
            const auto previous = entries_[i];
            entries_.removeAt(i);
            if (!save()) { entries_.insert(i, previous); return false; }
            if (!previous.thumbnailPath.isEmpty()) QFile::remove(previous.thumbnailPath);
            emit changed();
            return true;
        }
    return false;
}
bool WallpaperLibrary::relocate(const QString& id, const QString& path) {
    auto* e = find(id);
    QFileInfo file(path);
    if (!e || !file.exists() || !file.isFile())
        return false;
    const auto type = WallpaperEntry::typeForPath(path);
    if (type == WallpaperType::Unknown)
        return false;
    const auto previous = *e;
    e->path = file.absoluteFilePath();
    e->fileName = file.fileName();
    e->type = type;
    e->mimeType = QMimeDatabase().mimeTypeForFile(file).name();
    e->thumbnailPath = ThumbnailGenerator::create(*e);
    e->thumbnailVersion = 2;
    e->valid = true;
    if (!save()) {
        *e = previous;
        return false;
    }
    emit changed();
    return true;
}
void WallpaperLibrary::refreshValidity() {
    for (auto& e : entries_)
        e.valid = QFileInfo::exists(e.path);
    emit changed();
}
