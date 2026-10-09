#include "system/TranslucentTBHost.h"

#include "common/Logger.h"
#include "config/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>

TranslucentTBHost::TranslucentTBHost(QObject* parent)
    : QObject(parent), process_(new QProcess(this)),
      enabled_(AppSettings().translucentTbEnabled()) {
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (stopping_ || error == QProcess::Crashed)
            return;
        const QString message =
            QStringLiteral("透明任务栏组件无法启动：%1").arg(process_->errorString());
        Logger::warning(message);
        emit statusChanged(message);
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (stopping_)
                    return;
                const QString message = status == QProcess::NormalExit
                                            ? QStringLiteral("透明任务栏组件已退出（%1）").arg(code)
                                            : QStringLiteral("透明任务栏组件异常退出");
                Logger::warning(message);
                emit statusChanged(message);
            });
}

QString TranslucentTBHost::runtimeDirectory() const {
    const auto installed = QDir(QCoreApplication::applicationDirPath()).filePath("translucenttb");
    if (QFileInfo::exists(QDir(installed).filePath("TranslucentTB.exe")))
        return installed;
#ifdef LIGHTWALLPAPER_SOURCE_ROOT
    return QDir(QStringLiteral(LIGHTWALLPAPER_SOURCE_ROOT))
        .filePath("third_party/translucenttb-runtime");
#else
    return {};
#endif
}

QString TranslucentTBHost::executablePath() const {
    return QDir(runtimeDirectory()).filePath("TranslucentTB.exe");
}

bool TranslucentTBHost::writeConfiguration(QString* failureReason) const {
    const QString directory = runtimeDirectory();
    if (directory.isEmpty() || !QFileInfo::exists(executablePath())) {
        if (failureReason)
            *failureReason = QStringLiteral("未找到官方 TranslucentTB 运行组件");
        return false;
    }

    // This is the documented portable settings format. Static Clear mode is
    // deliberate: LightWallpaper owns a single user-visible toggle instead of
    // exposing TranslucentTB's dynamic per-window policy to a second UI.
    const QJsonObject config{
        {"desktop_appearance", QJsonObject{{"accent", "clear"},
                                           {"color", "#00000000"},
                                           {"show_peek", true},
                                           {"show_line", false}}},
        {"visible_window_appearance", QJsonObject{{"enabled", false}}},
        {"maximized_window_appearance", QJsonObject{{"enabled", false}}},
        {"start_opened_appearance", QJsonObject{{"enabled", false}}},
        {"search_opened_appearance", QJsonObject{{"enabled", false}}},
        {"task_view_opened_appearance", QJsonObject{{"enabled", false}}},
        {"battery_saver_appearance", QJsonObject{{"enabled", false}}},
        {"hide_tray", true},
        {"disable_saving", true},
        {"verbosity", "warn"},
        {"copy_dlls", false},
    };
    QSaveFile settings(QDir(directory).filePath("settings.json"));
    if (!settings.open(QIODevice::WriteOnly)) {
        if (failureReason)
            *failureReason =
                QStringLiteral("无法写入透明任务栏配置：%1").arg(settings.errorString());
        return false;
    }
    settings.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
    if (!settings.commit()) {
        if (failureReason)
            *failureReason =
                QStringLiteral("无法保存透明任务栏配置：%1").arg(settings.errorString());
        return false;
    }
    return true;
}

Result TranslucentTBHost::setEnabled(bool enabled) {
    enabled_ = enabled;
    AppSettings().setTranslucentTbEnabled(enabled);
    if (enabled) {
        start();
        return Result::success();
    }
    shutdown();
    emit statusChanged(QStringLiteral("透明任务栏已关闭"));
    return Result::success();
}

void TranslucentTBHost::startIfEnabled() {
    if (enabled_)
        start();
}

void TranslucentTBHost::start() {
    if (!enabled_ || process_->state() != QProcess::NotRunning)
        return;
    QString reason;
    if (!writeConfiguration(&reason)) {
        Logger::warning(reason);
        emit statusChanged(reason);
        return;
    }
    stopping_ = false;
    process_->setWorkingDirectory(runtimeDirectory());
    process_->setProgram(executablePath());
    process_->setArguments({});
    process_->start();
    if (!process_->waitForStarted(4000)) {
        const QString message =
            QStringLiteral("透明任务栏组件启动失败：%1").arg(process_->errorString());
        Logger::warning(message);
        emit statusChanged(message);
        return;
    }
    Logger::info("Official TranslucentTB helper started with hidden tray");
    emit statusChanged(QStringLiteral("透明任务栏已启用"));
}

void TranslucentTBHost::shutdown() {
    if (process_->state() == QProcess::NotRunning)
        return;
    stopping_ = true;
    process_->terminate();
    if (!process_->waitForFinished(2500)) {
        // QProcess owns this exact PID, so this cannot affect an independently
        // started TranslucentTB instance or another user's process.
        process_->kill();
        process_->waitForFinished(1000);
    }
    stopping_ = false;
}
