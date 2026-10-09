#pragma once
#include <QString>

class Logger final {
  public:
    static void initialize(bool enabled);
    static void info(const QString& message);
    static void warning(const QString& message);
    static void error(const QString& message);
};
