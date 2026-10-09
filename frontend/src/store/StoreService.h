#pragma once
#include "store/ApiClient.h"
#include <QJsonArray>
#include <QTimer>

class StoreService final : public QObject {
    Q_OBJECT
  public:
    explicit StoreService(QObject* parent = nullptr);
    ApiClient* api() { return &api_; }
    void configure(const QUrl& url, bool demo);
    void restoreSession();
    void login(const QString& username, const QString& password, bool remember);
    void registerAccount(const QString& username, const QString& password);
    void logout();
    void refreshCatalog(const QString& keyword = {}, const QString& category = {}, int page = 1);
    void fetchDetail(const QString& productId);
    void syncOwned();
    void fetchOrders();
    void createOrder(const QString& productId);
    void payTestOrder(const QString& orderId);
    void requestDownload(const QJsonObject& product);
    bool loggedIn() const { return !user_.isEmpty(); }
    bool demo() const { return demo_; }
    QString accountKey() const;
    QJsonObject user() const { return user_; }
    QJsonArray products() const { return products_; }
    QJsonArray owned() const { return owned_; }
    bool owns(const QString& id) const;
  signals:
    void sessionChanged();
    void catalogChanged(const QJsonArray& items, int page, int total);
    void categoriesChanged(const QJsonArray& items);
    void detailLoaded(const QJsonObject& product);
    void ownedChanged();
    void ordersLoaded(const QJsonArray& items);
    void orderCreated(const QJsonObject& order);
    void downloadAuthorized(const QJsonObject& product, const QJsonObject& grant, const QString& accountKey);
    void error(const QString& message);
    void message(const QString& message);
  private:
    void acceptSession(const QJsonObject& data);
    void refreshSession();
    void invalidateSession(bool eraseVault);
    bool report(const ApiResult& result);
    ApiClient api_;
    QTimer refreshTimer_;
    QJsonObject user_;
    QJsonArray products_, owned_;
    QString refreshToken_;
    bool demo_ = false;
    bool remember_ = false;
    quint64 generation_ = 0;
    quint64 catalogGeneration_ = 0;
};
