#include "store/StoreService.h"
#include "store/CredentialVault.h"
#include <QCryptographicHash>
#include <QUuid>

StoreService::StoreService(QObject* parent) : QObject(parent), api_(this) {
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_, &QTimer::timeout, this, &StoreService::refreshSession);
    connect(&api_, &ApiClient::authenticationExpired, this, [this] {
        invalidateSession(true);
        emit error("登录已过期，请重新登录；本地壁纸仍可使用。");
    });
}
void StoreService::configure(const QUrl& url, bool demo) {
    invalidateSession(false);
    demo_ = demo;
    api_.setBaseUrl(url);
    products_ = {};
    emit sessionChanged();
}
bool StoreService::report(const ApiResult& result) {
    if (!result.ok) {
        emit error(result.message + (result.requestId.isEmpty() ? QString{} : " [" + result.requestId + "]"));
        return false;
    }
    return true;
}
void StoreService::invalidateSession(bool eraseVault) {
    ++generation_;
    ++catalogGeneration_;
    api_.cancelAll();
    api_.setToken({});
    user_ = {};
    owned_ = {};
    refreshToken_.clear();
    refreshTimer_.stop();
    if (eraseVault && !demo_) CredentialVault::clear();
    emit sessionChanged();
    emit ownedChanged();
}
void StoreService::acceptSession(const QJsonObject& data) {
    const auto access = data.value("accessToken").toString();
    const auto refresh = data.value("refreshToken").toString();
    const auto user = data.value("user").toObject();
    if (access.isEmpty() || refresh.isEmpty() || user.value("id").toString().isEmpty()) {
        emit error("登录响应缺少 accessToken、refreshToken 或 user.id");
        return;
    }
    user_ = user;
    refreshToken_ = refresh;
    api_.setToken(access);
    if (remember_ && !demo_ && !CredentialVault::write({{"baseUrl", api_.baseUrl().toString()}, {"refreshToken", refreshToken_}}))
        emit error("登录成功，但无法加密保存登录凭证；下次启动需要重新登录。");
    const int lifetime = qBound(30, data.value("expiresIn").toInt(900), 86400);
    refreshTimer_.start(qMax(10, lifetime - 30) * 1000);
    emit sessionChanged();
    syncOwned();
}
void StoreService::restoreSession() {
    if (demo_) return;
    const auto saved = CredentialVault::read();
    if (saved.value("baseUrl").toString() != api_.baseUrl().toString()) return;
    refreshToken_ = saved.value("refreshToken").toString();
    remember_ = !refreshToken_.isEmpty();
    if (remember_) refreshSession();
}
void StoreService::refreshSession() {
    if (refreshToken_.isEmpty()) return;
    const auto epoch = generation_;
    api_.request("POST", "/auth/refresh", {{"refreshToken", refreshToken_}}, this, [this, epoch](const ApiResult& r) {
        if (epoch != generation_) return;
        if (!r.ok) {
            if (r.status == 401 || r.status == 403) invalidateSession(true);
            else refreshTimer_.start(30000);
            report(r);
            return;
        }
        acceptSession(r.data.toObject());
    });
}
void StoreService::login(const QString& username, const QString& password, bool remember) {
    invalidateSession(true);
    remember_ = remember;
    const auto epoch = generation_;
    api_.request("POST", "/auth/login", {{"username", username}, {"password", password}}, this,
                 [this, epoch](const ApiResult& r) { if (epoch == generation_ && report(r)) acceptSession(r.data.toObject()); });
}
void StoreService::registerAccount(const QString& username, const QString& password) {
    const auto epoch = generation_;
    api_.request("POST", "/auth/register", {{"username", username}, {"password", password}}, this,
                 [this, epoch](const ApiResult& r) { if (epoch == generation_ && report(r)) emit message("注册成功，请使用新账号登录。"); });
}
void StoreService::logout() {
    const auto refresh = refreshToken_;
    invalidateSession(true);
    if (!refresh.isEmpty()) api_.request("POST", "/auth/logout", {{"refreshToken", refresh}}, this, [](const ApiResult&) {});
}
QString StoreService::accountKey() const {
    if (!loggedIn()) return {};
    const auto scope = api_.baseUrl().toString().toUtf8() + '|' + user_.value("id").toString().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(scope, QCryptographicHash::Sha256).toHex());
}
bool StoreService::owns(const QString& id) const {
    for (const auto& value : owned_) if (value.toObject().value("id").toString() == id) return true;
    return false;
}
void StoreService::refreshCatalog(const QString& keyword, const QString& category, int page) {
    const auto epoch = generation_;
    const auto query = ++catalogGeneration_;
    const auto path = QString("/wallpapers?page=%1&pageSize=12&keyword=%2&categoryId=%3")
        .arg(page).arg(QString::fromUtf8(QUrl::toPercentEncoding(keyword)), QString::fromUtf8(QUrl::toPercentEncoding(category)));
    api_.request("GET", path, {}, this, [this, epoch, query, page](const ApiResult& r) {
        if (epoch != generation_ || query != catalogGeneration_) return;
        if (!report(r)) { emit catalogChanged({}, page, 0); return; }
        const auto data = r.data.toObject();
        products_ = data.value("items").toArray();
        emit catalogChanged(products_, data.value("page").toInt(page), data.value("total").toInt());
    });
    api_.request("GET", "/categories", {}, this, [this, epoch](const ApiResult& r) {
        if (epoch == generation_ && r.ok) emit categoriesChanged(r.data.toArray());
    });
}
void StoreService::fetchDetail(const QString& id) {
    const auto epoch = generation_;
    api_.request("GET", "/wallpapers/" + QString::fromUtf8(QUrl::toPercentEncoding(id)), {}, this, [this, epoch](const ApiResult& r) {
        if (epoch == generation_ && report(r)) emit detailLoaded(r.data.toObject());
    });
}
void StoreService::syncOwned() {
    if (!loggedIn()) return;
    const auto epoch = generation_;
    api_.request("GET", "/me/wallpapers", {}, this, [this, epoch](const ApiResult& r) {
        if (epoch != generation_ || !report(r)) return;
        owned_ = r.data.toArray();
        emit ownedChanged();
    }, true);
}
void StoreService::fetchOrders() {
    if (!loggedIn()) { emit error("请先登录后查看订单"); return; }
    const auto epoch = generation_;
    api_.request("GET", "/me/orders", {}, this, [this, epoch](const ApiResult& r) {
        if (epoch == generation_ && report(r)) emit ordersLoaded(r.data.toArray());
    }, true);
}
void StoreService::createOrder(const QString& id) {
    if (!loggedIn()) { emit error("请先登录后购买或领取壁纸"); return; }
    const auto epoch = generation_;
    api_.request("POST", "/orders", {{"wallpaperId", id}}, this, [this, epoch](const ApiResult& r) {
        if (epoch == generation_ && report(r)) { emit orderCreated(r.data.toObject()); syncOwned(); }
    }, true, QUuid::createUuid().toString(QUuid::WithoutBraces));
}
void StoreService::payTestOrder(const QString& id) {
    const auto epoch = generation_;
    api_.request("POST", "/orders/" + id + "/test-pay", {}, this, [this, epoch](const ApiResult& r) {
        if (epoch == generation_ && report(r)) { emit message("测试订单已完成（未发生真实扣款）"); syncOwned(); fetchOrders(); }
    }, true, QUuid::createUuid().toString(QUuid::WithoutBraces));
}
void StoreService::requestDownload(const QJsonObject& product) {
    if (!loggedIn()) { emit error("请先登录后下载"); return; }
    const auto epoch = generation_;
    const auto key = accountKey();
    api_.request("POST", "/wallpapers/" + product.value("id").toString() + "/download-grants", {}, this,
                 [this, epoch, product, key](const ApiResult& r) {
        if (epoch == generation_ && report(r)) emit downloadAuthorized(product, r.data.toObject(), key);
    }, true);
}
