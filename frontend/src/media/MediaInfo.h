#pragma once
#include "common/Result.h"
#include <QSize>
#include <QString>

struct MediaInfo {
    QSize resolution;
    qint64 bytes = 0;
    qint64 durationMs = 0;
    QString codecHint;
};
class MediaInspector final {
  public:
    static Result inspect(const QString& path, MediaInfo* info);
};
