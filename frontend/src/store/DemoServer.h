#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTcpServer>
#include <QUrl>
class QTcpSocket;

// Development fixture only. It deliberately implements the real HTTP contract.
class DemoServer final : public QObject {
    Q_OBJECT
  public:
    explicit DemoServer(QObject* parent = nullptr);
    bool start();
    QUrl baseUrl() const;
    void setCorruptDownloads(bool value) { corruptDownloads_ = value; }
  private:
    void serve(QTcpSocket* socket);
    void dispatch(QTcpSocket* socket, const QByteArray& method, const QUrl& target,
                  const QHash<QByteArray, QByteArray>& headers, const QJsonObject& body);
    void reply(QTcpSocket* socket, int status, const QJsonValue& data,
               const QString& code = "OK", const QString& message = {});
    void bytes(QTcpSocket* socket, const QByteArray& body, const QByteArray& contentType);
    QJsonObject session(const QString& userId);
    QString authenticated(const QHash<QByteArray, QByteArray>& headers) const;
    QJsonObject product(const QString& id) const;
    QTcpServer server_;
    QJsonArray products_;
    QHash<QString, QByteArray> images_;
    QHash<QString, QJsonObject> users_;
    QHash<QString, QString> accessTokens_, refreshTokens_;
    QHash<QString, QSet<QString>> owned_;
    QHash<QString, QJsonObject> orders_;
    QHash<QString, QJsonObject> tickets_;
    bool corruptDownloads_ = false;
};
