#pragma once
#include "common/Result.h"
class AutoStartManager final {
  public:
    static bool isEnabled();
    static Result setEnabled(bool enabled);
};
