#include "store/ApiClient.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

ApiClient::ApiClient(QObject* parent) : QObject(parent) { network_.setTransferTimeout(15000); }
bool ApiClient::safeUrl(const QUrl& url) {
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
           (url.scheme() == "https" || (url.scheme() == "http" &&
             (url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1")));
}
void ApiClient::setBaseUrl(const QUrl& url) {
    cancelAll();
    baseUrl_ = url;
    token_.clear();
}
void ApiClient::cancelAll() {
    const auto replies = pending_;
    for (auto* reply : replies) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    pending_.clear();
}
void ApiClient::request(const QByteArray& method, const QString& path, const QJsonObject& body,
                        QObject* context, Callback callback, bool authenticated,
                        const QString& idempotencyKey) {
    QUrl url(baseUrl_.toString(QUrl::RemoveQuery | QUrl::RemoveFragment).remove(QRegularExpression("/+$")) + path);
    if (!safeUrl(url) || (authenticated && token_.isEmpty())) {
        ApiResult result;
        result.code = "CLIENT_CONFIGURATION";
        result.message = authenticated && token_.isEmpty() ? "请先登录" : "接口地址必须使用 HTTPS 或本机 HTTP";
        QTimer::singleShot(0, context, [callback, result] { callback(result); });
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Client-Version", "0.2.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (authenticated) request.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    if (!idempotencyKey.isEmpty()) request.setRawHeader("Idempotency-Key", idempotencyKey.toUtf8());
    auto* reply = network_.sendCustomRequest(request, method, method == "GET" ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
    pending_.insert(reply);
    QPointer<QObject> guard(context);
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 4 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, guard, callback, authenticated] {
        pending_.remove(reply);
        ApiResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &parse);
        const auto envelope = doc.object();
        result.code = envelope.value("code").toString();
        result.message = envelope.value("message").toString();
        result.requestId = envelope.value("requestId").toString();
        result.data = envelope.value("data");
        result.ok = result.status >= 200 && result.status < 300 && reply->error() == QNetworkReply::NoError &&
                    parse.error == QJsonParseError::NoError && result.code == "OK";
        if (!result.ok && result.message.isEmpty())
            result.message = result.status == 0 ? "无法连接后端：" + reply->errorString() : "后端响应格式无效或请求失败（HTTP " + QString::number(result.status) + "）";
        reply->deleteLater();
        if (!guard) return;
        if (authenticated && result.status == 401) emit authenticationExpired();
        callback(result);
    });
}
