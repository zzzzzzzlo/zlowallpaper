#pragma once
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <memory>
class WallpaperLibrary;
struct DownloadTask;

class DownloadManager final : public QObject {
    Q_OBJECT
  public:
    explicit DownloadManager(WallpaperLibrary* library, QObject* parent = nullptr);
    ~DownloadManager() override;
    void start(const QJsonObject& product, const QJsonObject& grant, const QString& accountKey);
    void cancel(const QString& productId);
    void cancelAll();
    bool busy(const QString& productId) const;
    QString state(const QString& productId) const { return states_.value(productId); }
    void resetStates();
  signals:
    void progress(const QString& productId, qint64 received, qint64 total);
    void stateChanged(const QString& productId, const QString& state, const QString& message);
    void imported(const QString& localEntryId);
  private:
    void fail(const std::shared_ptr<DownloadTask>& task, const QString& message);
    WallpaperLibrary* library_;
    QNetworkAccessManager network_;
    QHash<QString, std::shared_ptr<DownloadTask>> tasks_;
    QHash<QString, QString> states_;
};
