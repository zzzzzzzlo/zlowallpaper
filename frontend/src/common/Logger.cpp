#include "common/Logger.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

namespace {
QMutex mutex;
bool enabled = true;
constexpr qint64 MaxBytes = 5 * 1024 * 1024;
} // namespace
static void write(const QString& level, const QString& message) {
    if (!enabled)
        return;
    QMutexLocker lock(&mutex);
    const auto root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    QDir().mkpath(root);
    const auto path = root + "/lightwallpaper.log";
    QFile f(path);
    if (f.exists() && f.size() > MaxBytes) {
        QFile::remove(root + "/lightwallpaper.3.log");
        QFile::rename(root + "/lightwallpaper.2.log", root + "/lightwallpaper.3.log");
        QFile::rename(root + "/lightwallpaper.1.log", root + "/lightwallpaper.2.log");
        QFile::rename(path, root + "/lightwallpaper.1.log");
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << " [" << level << "] "
            << message << '\n';
    }
}
void Logger::initialize(bool value) {
    enabled = value;
}
void Logger::info(const QString& m) {
    write("INFO", m);
}
void Logger::warning(const QString& m) {
    write("WARN", m);
}
void Logger::error(const QString& m) {
    write("ERROR", m);
}
