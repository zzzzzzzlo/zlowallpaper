#pragma once
#include "library/WallpaperEntry.h"
class ThumbnailGenerator final {
  public:
    static QString create(const WallpaperEntry& entry);
};
