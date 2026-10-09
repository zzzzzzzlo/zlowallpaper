#include "store/DownloadManager.h"
#include "store/ApiClient.h"
#include "library/WallpaperLibrary.h"
#include "media/ThumbnailGenerator.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QPointer>
#include "common/AtomicFile.h"
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>

struct DownloadTask {
    QString id, accountKey, version, sha256, path;
    QJsonObject product;
    qint64 expected = 0, received = 0;
    bool canceled = false, writeFailed = false;
    QPointer<QNetworkReply> reply;
    std::unique_ptr<AtomicFile> file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
};

DownloadManager::DownloadManager(WallpaperLibrary* library, QObject* parent)
    : QObject(parent), library_(library) { network_.setTransferTimeout(30000); }
DownloadManager::~DownloadManager() { cancelAll(); }
bool DownloadManager::busy(const QString& id) const { return tasks_.contains(id); }
void DownloadManager::resetStates() { cancelAll(); states_.clear(); }
void DownloadManager::cancelAll() {
    const auto keys = tasks_.keys();
    for (const auto& key : keys) cancel(key);
}
void DownloadManager::cancel(const QString& id) {
    auto task = tasks_.take(id);
    if (!task) return;
    task->canceled = true;
    if (task->file) task->file->cancelWriting();
    if (task->reply) task->reply->abort();
    states_[id] = "canceled";
    emit stateChanged(id, "canceled", "下载已取消，可以重新下载");
}
void DownloadManager::fail(const std::shared_ptr<DownloadTask>& task, const QString& message) {
    if (task->file) task->file->cancelWriting();
    tasks_.remove(task->id);
    states_[task->id] = "failed";
    emit stateChanged(task->id, "failed", message);
}
void DownloadManager::start(const QJsonObject& product, const QJsonObject& grant, const QString& accountKey) {
    const auto id = product.value("id").toString();
    if (busy(id)) return;
    auto task = std::make_shared<DownloadTask>();
    task->id = id;
    task->accountKey = accountKey;
    task->product = product;
    task->version = grant.value("resourceVersion").toString();
    task->expected = grant.value("sizeBytes").toString().toLongLong();
    task->sha256 = grant.value("sha256").toString().toLower();
    const auto ext = grant.value("fileExtension").toString().toLower();
    const QUrl url(grant.value("url").toString());
    const auto expires = QDateTime::fromString(grant.value("expiresAt").toString(), Qt::ISODate);
    if (id.isEmpty() || accountKey.isEmpty() || grant.value("wallpaperId").toString() != id ||
        task->version.isEmpty() || task->version != product.value("resourceVersion").toString() ||
        task->expected <= 0 || task->expected > 10LL * 1024 * 1024 * 1024 ||
        task->sha256.size() != 64 || QByteArray::fromHex(task->sha256.toLatin1()).size() != 32 ||
        WallpaperEntry::typeForPath("file." + ext) == WallpaperType::Unknown || !ApiClient::safeUrl(url) ||
        !expires.isValid() || expires <= QDateTime::currentDateTimeUtc()) {
        fail(task, "下载授权字段无效或已过期，请刷新后重试"); return;
    }
    if (tasks_.size() >= 3) { fail(task, "最多同时下载 3 张壁纸，请等待其他任务完成"); return; }
    if (const auto* existing = library_->findStoreEntry(id, task->version, accountKey)) {
        states_[id] = "completed";
        emit stateChanged(id, "completed", "该版本已经下载，可以应用");
        emit imported(existing->id);
        return;
    }
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/downloads/" + accountKey;
    if (!QDir().mkpath(directory)) { fail(task, "无法创建下载目录，请检查磁盘权限"); return; }
    const auto key = QCryptographicHash::hash((id + "|" + task->version).toUtf8(), QCryptographicHash::Sha256).toHex();
    task->path = directory + "/" + key + "." + ext;
    task->file = std::make_unique<AtomicFile>(task->path);
    if (!task->file->open(QIODevice::WriteOnly)) { fail(task, "无法写入下载文件：" + task->file->errorString()); return; }
    tasks_[id] = task;
    states_[id] = "downloading";
    emit stateChanged(id, "downloading", "正在下载");
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // Ticket URL authorizes this request. Never forward account credentials to storage/CDN.
    auto* reply = network_.get(request);
    task->reply = reply;
    connect(reply, &QIODevice::readyRead, this, [this, task] {
        if (task->canceled) return;
        auto bytes = task->reply->readAll();
        task->received += bytes.size();
        if (task->received > task->expected || task->file->write(bytes) != bytes.size()) {
            task->writeFailed = true;
            task->reply->abort(); return;
        }
        task->hash.addData(bytes);
        emit progress(task->id, task->received, task->expected);
    });
    connect(reply, &QNetworkReply::finished, this, [this, task, reply] {
        reply->deleteLater();
        if (task->canceled) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (task->writeFailed || reply->error() != QNetworkReply::NoError || status != 200) {
            fail(task, task->writeFailed ? "写入失败或文件超出授权大小，请检查磁盘空间" : "下载失败（HTTP " + QString::number(status) + "）：" + reply->errorString()); return;
        }
        if (task->received != task->expected || task->hash.result().toHex() != task->sha256.toLatin1()) {
            fail(task, "文件大小或 SHA-256 校验失败，未加入壁纸库，请重新下载"); return;
        }
        if (!task->file->commit()) { fail(task, "无法提交下载文件：" + task->file->errorString()); return; }
        task->file.reset();
        states_[task->id] = "importing";
        emit stateChanged(task->id, "importing", "校验完成，正在生成缩略图");
        WallpaperEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.path = task->path;
        entry.fileName = task->product.value("title").toString();
        entry.type = WallpaperEntry::typeForPath(entry.path);
        entry.valid = true;
        entry.addedAt = QDateTime::currentDateTimeUtc();
        entry.storeProductId = task->id;
        entry.storeResourceVersion = task->version;
        entry.storeAccountKey = task->accountKey;
        entry.thumbnailVersion = 2;
        auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, task, entry]() mutable {
            entry.thumbnailPath = watcher->result();
            watcher->deleteLater();
            if (task->canceled) { QFile::remove(entry.thumbnailPath); return; }
            const auto result = library_->addPrepared(entry);
            if (!result.ok) { fail(task, result.message); return; }
            tasks_.remove(task->id);
            states_[task->id] = "completed";
            emit stateChanged(task->id, "completed", "已下载并加入本地库");
            emit imported(entry.id);
        });
        watcher->setFuture(QtConcurrent::run([entry] { return ThumbnailGenerator::create(entry); }));
    });
}
