#pragma once
#include <QJsonObject>
namespace CredentialVault {
QJsonObject read();
bool write(const QJsonObject& credentials);
void clear();
}
