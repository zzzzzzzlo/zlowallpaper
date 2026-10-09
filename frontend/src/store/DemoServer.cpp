#include "store/DemoServer.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QImage>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUuid>

namespace {
QString randomId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString passwordHash(const QString& password, const QString& salt) {
    return QCryptographicHash::hash((salt + password).toUtf8(), QCryptographicHash::Sha256).toHex();
}
}
DemoServer::DemoServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = server_.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { serve(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}
bool DemoServer::start() {
    if (server_.isListening()) return true;
    if (!server_.listen(QHostAddress::LocalHost, 0)) return false;
    const QStringList titles{"雾山 · 清晨", "潮汐 · 月光", "沙丘 · 暮色", "森林 · 微光", "雪原 · 静谧", "城市 · 蓝调"};
    const QStringList colors{"#95b8b0", "#516b98", "#b48d73", "#709187", "#a5becb", "#697ca5"};
    for (int i = 0; i < titles.size(); ++i) {
        const QString id = "demo-" + QString::number(i + 1);
        QImage image(1600, 900, QImage::Format_RGB32);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient sky(0, 0, 0, 900);
        const QColor color(colors[i]);
        sky.setColorAt(0, color.lighter(155)); sky.setColorAt(1, color.darker(120));
        painter.fillRect(image.rect(), sky);
        painter.setBrush(QColor(255, 248, 227, 190)); painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(1130, 230), 82, 82);
        for (int layer = 0; layer < 4; ++layer) {
            QPainterPath mountain; mountain.moveTo(0, 610 + layer * 60);
            mountain.cubicTo(320, 320 + layer * 100, 470, 880 - layer * 60, 790, 480 + layer * 70);
            mountain.cubicTo(1120, 220 + layer * 120, 1390, 810 - layer * 70, 1600, 540 + layer * 60);
            mountain.lineTo(1600, 900); mountain.lineTo(0, 900); mountain.closeSubpath();
            QColor ridge = color.darker(105 + layer * 25); ridge.setAlpha(180);
            painter.fillPath(mountain, ridge);
        }
        painter.end();
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        images_[id] = png;
        const auto origin = baseUrl().adjusted(QUrl::RemovePath).toString();
        products_.append(QJsonObject{{"id", id}, {"title", titles[i]}, {"description", "演示程序生成的壁纸样例，用于测试浏览、订单和下载流程。非线上商品，不发生实际扣款。"},
            {"categoryId", i == 5 ? "city" : "nature"}, {"categoryName", i == 5 ? "城市" : "自然"}, {"type", "IMAGE"},
            {"priceCents", i % 3 == 0 ? 0 : 1200 + i * 100}, {"currency", "CNY"}, {"coverUrl", origin + "/covers/" + id},
            {"width", 1600}, {"height", 900}, {"fps", 0}, {"resourceVersion", "1"}, {"sizeBytes", QString::number(png.size())}, {"creator", "Zlo 演示工作室"}});
    }
    const QString salt = randomId();
    users_["demo"] = {{"id", "demo-user"}, {"username", "demo"}, {"displayName", "演示用户"}, {"salt", salt}, {"passwordHash", passwordHash("Demo12345", salt)}};
    return true;
}
QUrl DemoServer::baseUrl() const { return QUrl("http://127.0.0.1:" + QString::number(server_.serverPort()) + "/api/v1"); }
void DemoServer::serve(QTcpSocket* socket) {
    if (socket->property("handled").toBool()) return;
    auto raw = socket->property("input").toByteArray() + socket->readAll();
    if (raw.size() > 1024 * 1024) { socket->disconnectFromHost(); return; }
    socket->setProperty("input", raw);
    const auto boundary = raw.indexOf("\r\n\r\n");
    if (boundary < 0) return;
    const auto lines = raw.left(boundary).split('\n');
    const auto first = lines.first().trimmed().split(' ');
    if (first.size() < 2) { socket->disconnectFromHost(); return; }
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed(); const int colon = line.indexOf(':');
        if (colon > 0) headers[line.left(colon).toLower()] = line.mid(colon + 1).trimmed();
    }
    const int length = headers.value("content-length").toInt();
    if (length < 0 || length > 1024 * 1024) { socket->disconnectFromHost(); return; }
    if (raw.size() < boundary + 4 + length) return;
    socket->setProperty("handled", true);
    dispatch(socket, first[0], QUrl::fromEncoded(first[1]), headers, QJsonDocument::fromJson(raw.mid(boundary + 4, length)).object());
}
void DemoServer::bytes(QTcpSocket* socket, const QByteArray& body, const QByteArray& contentType) {
    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
}
void DemoServer::reply(QTcpSocket* socket, int status, const QJsonValue& data, const QString& code, const QString& message) {
    const auto payload = QJsonDocument(QJsonObject{{"code", code}, {"message", message}, {"data", data}, {"requestId", randomId()}}).toJson(QJsonDocument::Compact);
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Response\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
    socket->disconnectFromHost();
}
QString DemoServer::authenticated(const QHash<QByteArray, QByteArray>& headers) const {
    const auto authorization = headers.value("authorization");
    if (!authorization.startsWith("Bearer ")) return {};
    return accessTokens_.value(QString::fromUtf8(authorization.mid(7)));
}
QJsonObject DemoServer::product(const QString& id) const {
    for (const auto& value : products_) if (value.toObject().value("id").toString() == id) return value.toObject();
    return {};
}
QJsonObject DemoServer::session(const QString& id) {
    const auto access = randomId(), refresh = randomId();
    accessTokens_[access] = id; refreshTokens_[refresh] = id;
    QJsonObject user;
    for (const auto& candidate : users_) if (candidate.value("id").toString() == id)
        user = {{"id", id}, {"username", candidate.value("username")}, {"displayName", candidate.value("displayName")}};
    return {{"accessToken", access}, {"refreshToken", refresh}, {"expiresIn", 900}, {"user", user}};
}
void DemoServer::dispatch(QTcpSocket* socket, const QByteArray& method, const QUrl& target,
                           const QHash<QByteArray, QByteArray>& headers, const QJsonObject& body) {
    const auto path = target.path();
    if (path.startsWith("/covers/") && method == "GET") {
        const auto id = path.mid(8); if (images_.contains(id)) { bytes(socket, images_[id], "image/png"); return; }
    }
    if (path.startsWith("/files/") && method == "GET") {
        const auto ticket = tickets_.value(path.mid(7));
        if (ticket.isEmpty() || QDateTime::fromString(ticket.value("expiresAt").toString(), Qt::ISODate) <= QDateTime::currentDateTimeUtc()) {
            reply(socket, 403, {}, "DOWNLOAD_EXPIRED", "下载凭证无效或已过期"); return;
        }
        auto file = images_.value(ticket.value("id").toString());
        if (corruptDownloads_ && !file.isEmpty()) file[0] = char(file[0] ^ 1);
        bytes(socket, file, "image/png"); return;
    }
    if (!path.startsWith("/api/v1/")) { reply(socket, 404, {}, "NOT_FOUND", "接口不存在"); return; }
    const auto route = path.mid(7);
    if (route == "/auth/register" && method == "POST") {
        const auto name = body.value("username").toString(), password = body.value("password").toString();
        if (name.size() < 3 || name.size() > 32 || password.size() < 8 || password.size() > 128) {
            reply(socket, 422, {}, "VALIDATION_ERROR", "用户名为 3–32 个字符，密码为 8–128 个字符"); return;
        }
        if (users_.contains(name)) { reply(socket, 409, {}, "USERNAME_EXISTS", "用户名已存在"); return; }
        const auto salt = randomId();
        users_[name] = {{"id", randomId()}, {"username", name}, {"displayName", name}, {"salt", salt}, {"passwordHash", passwordHash(password, salt)}};
        reply(socket, 201, QJsonObject{{"username", name}}); return;
    }
    if (route == "/auth/login" && method == "POST") {
        const auto user = users_.value(body.value("username").toString());
        if (user.isEmpty() || user.value("passwordHash").toString() != passwordHash(body.value("password").toString(), user.value("salt").toString())) {
            reply(socket, 401, {}, "INVALID_CREDENTIALS", "账号或密码错误"); return;
        }
        reply(socket, 200, session(user.value("id").toString())); return;
    }
    if (route == "/auth/refresh" && method == "POST") {
        const auto id = refreshTokens_.take(body.value("refreshToken").toString());
        if (id.isEmpty()) { reply(socket, 401, {}, "SESSION_EXPIRED", "请重新登录"); return; }
        reply(socket, 200, session(id)); return;
    }
    if (route == "/auth/logout" && method == "POST") {
        const auto id = refreshTokens_.take(body.value("refreshToken").toString());
        const auto tokens = accessTokens_.keys();
        for (const auto& token : tokens) if (accessTokens_[token] == id) accessTokens_.remove(token);
        reply(socket, 200, QJsonObject{}); return;
    }
    if (route == "/categories" && method == "GET") {
        reply(socket, 200, QJsonArray{QJsonObject{{"id", "nature"}, {"name", "自然"}}, QJsonObject{{"id", "city"}, {"name", "城市"}}}); return;
    }
    if (route == "/wallpapers" && method == "GET") {
        QUrlQuery query(target);
        const auto keyword = query.queryItemValue("keyword"), category = query.queryItemValue("categoryId");
        QJsonArray matching, items;
        for (const auto& value : products_) {
            const auto p = value.toObject();
            if (p.value("title").toString().contains(keyword, Qt::CaseInsensitive) && (category.isEmpty() || category == p.value("categoryId").toString())) matching.append(p);
        }
        const int page = qMax(1, query.queryItemValue("page").toInt()), limit = qBound(1, query.queryItemValue("pageSize").toInt(), 50);
        for (int i = (page - 1) * limit; i < qMin(page * limit, int(matching.size())); ++i) items.append(matching[i]);
        reply(socket, 200, QJsonObject{{"items", items}, {"page", page}, {"pageSize", limit}, {"total", int(matching.size())}}); return;
    }
    if (route.startsWith("/wallpapers/") && !route.endsWith("/download-grants") && method == "GET") {
        const auto p = product(route.mid(12));
        if (p.isEmpty()) reply(socket, 404, {}, "PRODUCT_NOT_FOUND", "商品不存在"); else reply(socket, 200, p);
        return;
    }
    const auto uid = authenticated(headers);
    if (uid.isEmpty()) { reply(socket, 401, {}, "SESSION_EXPIRED", "请先登录"); return; }
    if (route == "/me" && method == "GET") {
        for (const auto& u : users_) if (u.value("id").toString() == uid) { reply(socket, 200, QJsonObject{{"id", uid}, {"username", u.value("username")}, {"displayName", u.value("displayName")}}); return; }
    }
    if (route == "/me/wallpapers" && method == "GET") {
        QJsonArray list; for (const auto& p : products_) if (owned_[uid].contains(p.toObject().value("id").toString())) list.append(p);
        reply(socket, 200, list); return;
    }
    if (route == "/me/orders" && method == "GET") {
        QJsonArray list; for (const auto& o : orders_) if (o.value("userId").toString() == uid) list.append(o);
        reply(socket, 200, list); return;
    }
    if (route == "/orders" && method == "POST") {
        const auto id = body.value("wallpaperId").toString();
        const auto p = product(id);
        if (p.isEmpty()) { reply(socket, 404, {}, "PRODUCT_NOT_FOUND", "商品不存在"); return; }
        if (headers.value("idempotency-key").isEmpty()) { reply(socket, 422, {}, "IDEMPOTENCY_REQUIRED", "缺少幂等键"); return; }
        for (const auto& existing : orders_) if (existing.value("userId").toString() == uid && existing.value("wallpaperId").toString() == id) { reply(socket, 200, existing); return; }
        const auto orderId = randomId(); const bool free = p.value("priceCents").toInt() == 0;
        const QJsonObject order{{"id", orderId}, {"userId", uid}, {"wallpaperId", id}, {"title", p.value("title")}, {"amountCents", p.value("priceCents")}, {"currency", "CNY"}, {"status", free ? "PAID" : "PENDING"}, {"testPaymentEnabled", true}, {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
        orders_[orderId] = order; if (free) owned_[uid].insert(id);
        reply(socket, 201, order); return;
    }
    if (route.startsWith("/orders/") && route.endsWith("/test-pay") && method == "POST") {
        const auto id = route.mid(8, route.size() - 8 - 9);
        auto order = orders_.value(id);
        if (order.isEmpty() || order.value("userId").toString() != uid) { reply(socket, 404, {}, "ORDER_NOT_FOUND", "订单不存在"); return; }
        order["status"] = "PAID"; orders_[id] = order; owned_[uid].insert(order.value("wallpaperId").toString());
        reply(socket, 200, order); return;
    }
    if (route.startsWith("/wallpapers/") && route.endsWith("/download-grants") && method == "POST") {
        const auto id = route.mid(12, route.size() - 12 - 16);
        const auto p = product(id);
        if (!owned_[uid].contains(id)) { reply(socket, 403, {}, "NOT_OWNED", "尚未拥有该壁纸，请先购买或领取"); return; }
        const auto ticket = randomId(), expiry = QDateTime::currentDateTimeUtc().addSecs(300).toString(Qt::ISODate);
        tickets_[ticket] = {{"id", id}, {"expiresAt", expiry}};
        reply(socket, 200, QJsonObject{{"wallpaperId", id}, {"resourceVersion", p.value("resourceVersion")}, {"url", baseUrl().adjusted(QUrl::RemovePath).toString() + "/files/" + ticket}, {"expiresAt", expiry}, {"fileExtension", "png"}, {"sizeBytes", QString::number(images_[id].size())}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(images_[id], QCryptographicHash::Sha256).toHex())}}); return;
    }
    reply(socket, 404, {}, "NOT_FOUND", "接口不存在");
}
