#pragma once
#include "common/Result.h"
#include "library/WallpaperEntry.h"
#include <QObject>

class WallpaperLibrary final : public QObject {
    Q_OBJECT
  public:
    explicit WallpaperLibrary(QObject* parent = nullptr, const QString& storageDirectory = {});
    void load();
    bool save(QString* error = nullptr) const;
    const QList<WallpaperEntry>& entries() const {
        return entries_;
    }
    WallpaperEntry* find(const QString& id);
    const WallpaperEntry* find(const QString& id) const;
    Result add(const QString& path);
    Result addPrepared(const WallpaperEntry& entry);
    const WallpaperEntry* findStoreEntry(const QString& productId, const QString& version,
                                        const QString& accountKey) const;
    bool remove(const QString& id);
    bool relocate(const QString& id, const QString& path);
    void refreshValidity();
  signals:
    void changed();

  private:
    QString filePath() const;
    QList<WallpaperEntry> entries_;
    QString storageDirectory_;
};
