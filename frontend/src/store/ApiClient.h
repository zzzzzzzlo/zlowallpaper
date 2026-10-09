#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>
#include <functional>

struct ApiResult {
    bool ok = false;
    int status = 0;
    QString code;
    QString message;
    QString requestId;
    QJsonValue data;
};

class ApiClient final : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(const ApiResult&)>;
    explicit ApiClient(QObject* parent = nullptr);
    void setBaseUrl(const QUrl& url);
    QUrl baseUrl() const { return baseUrl_; }
    void setToken(const QString& token) { token_ = token; }
    void request(const QByteArray& method, const QString& path, const QJsonObject& body,
                 QObject* context, Callback callback, bool authenticated = false,
                 const QString& idempotencyKey = {});
    void cancelAll();
    static bool safeUrl(const QUrl& url);
  signals:
    void authenticationExpired();
  private:
    QUrl baseUrl_;
    QString token_;
    QNetworkAccessManager network_;
    QSet<QNetworkReply*> pending_;
};
