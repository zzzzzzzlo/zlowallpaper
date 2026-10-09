#include "store/CredentialVault.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include "common/AtomicFile.h"
#include <QStandardPaths>
#include <windows.h>
#include <wincrypt.h>

namespace {
QString filePath() {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + "/session.dpapi";
}
}
QJsonObject CredentialVault::read() {
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return {};
    auto bytes = file.readAll();
    DATA_BLOB source{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, output{};
    if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    QByteArray plain(reinterpret_cast<const char*>(output.pbData), output.cbData);
    const auto value = QJsonDocument::fromJson(plain).object();
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    plain.fill('\0');
    return value;
}
bool CredentialVault::write(const QJsonObject& credentials) {
    auto plain = QJsonDocument(credentials).toJson(QJsonDocument::Compact);
    DATA_BLOB source{DWORD(plain.size()), reinterpret_cast<BYTE*>(plain.data())}, output{};
    const bool encrypted = CryptProtectData(&source, L"ZloWallpaper refresh credential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    plain.fill('\0');
    if (!encrypted) return false;
    AtomicFile file(filePath());
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(reinterpret_cast<const char*>(output.pbData), output.cbData) == output.cbData && file.commit();
    LocalFree(output.pbData);
    return ok;
}
void CredentialVault::clear() { QFile::remove(filePath()); }
