#include "icontra/IcontraHost.h"

#include "common/Logger.h"
#include "config/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <windows.h>

namespace {
constexpr int ProtocolVersion = 1;

QString requestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString secureToken() {
    // Two UUIDs give the local pipe handshake substantially more than 128 bits
    // of entropy without ever logging the secret.
    return QUuid::createUuid().toString(QUuid::WithoutBraces) +
           QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString sourceRoot() {
#ifdef LIGHTWALLPAPER_SOURCE_ROOT
    return QString::fromUtf8(LIGHTWALLPAPER_SOURCE_ROOT);
#else
    return QDir::currentPath();
#endif
}
} // namespace

IcontraHost::IcontraHost(QObject* parent)
    : QObject(parent), server_(new QLocalServer(this)), process_(new QProcess(this)),
      shutdownTimer_(new QTimer(this)), restartTimer_(new QTimer(this)) {
    shutdownTimer_->setSingleShot(true);
    restartTimer_->setSingleShot(true);

    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (server_->hasPendingConnections()) {
            auto* candidate = server_->nextPendingConnection();
            if (socket_) {
                candidate->disconnectFromServer();
                candidate->deleteLater();
                Logger::warning("Rejected duplicate Icontra IPC connection");
                continue;
            }
            socket_ = candidate;
            authenticated_ = false;
            receiveBuffer_.clear();
            connect(socket_, &QLocalSocket::readyRead, this, &IcontraHost::consumeSocket);
            connect(socket_, &QLocalSocket::disconnected, this, [this] {
                if (!socket_)
                    return;
                socket_->deleteLater();
                socket_ = nullptr;
                authenticated_ = false;
                receiveBuffer_.clear();
                if (!shuttingDown_ && process_->state() != QProcess::NotRunning) {
                    Logger::warning("Icontra IPC disconnected; stopping owned child process");
                    process_->terminate();
                }
            });
        }
    });

    connect(process_, &QProcess::started, this, [this] {
        assignChildToJob();
        setStatus("图标栏正在连接");
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto text = QString::fromUtf8(process_->readAllStandardOutput()).trimmed();
        if (!text.isEmpty())
            Logger::info("Icontra: " + text.left(1000));
    });
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        const auto text = QString::fromUtf8(process_->readAllStandardError()).trimmed();
        if (!text.isEmpty())
            Logger::warning("Icontra: " + text.left(1000));
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        const auto message = "Icontra process error: " + process_->errorString();
        Logger::warning(message);
        emit warning(message);
        if (error == QProcess::FailedToStart && !shuttingDown_)
            scheduleRestart(message);
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                const auto message =
                    QString("Icontra exited: code=%1 status=%2")
                        .arg(exitCode)
                        .arg(exitStatus == QProcess::NormalExit ? "normal" : "crash");
                Logger::info(message);
                setReady(false);
                if (socket_)
                    socket_->disconnectFromServer();
                if (shuttingDown_) {
                    shutdownTimer_->stop();
                    closeServer();
                    setStatus(enabled_ ? "图标栏已停止" : "图标栏已禁用");
                    return;
                }
                if (enabled_)
                    scheduleRestart(message);
            });
    connect(shutdownTimer_, &QTimer::timeout, this, [this] {
        Logger::warning("Icontra graceful shutdown timed out; terminating owned child");
        stopOwnedChild();
        closeServer();
    });
    connect(restartTimer_, &QTimer::timeout, this, [this] {
        if (enabled_ && !shuttingDown_)
            start();
    });
}

IcontraHost::~IcontraHost() {
    shuttingDown_ = true;
    stopOwnedChild();
    closeServer();
    if (job_)
        CloseHandle(static_cast<HANDLE>(job_));
}

qint64 IcontraHost::processId() const {
    return process_ ? process_->processId() : 0;
}

bool IcontraHost::keepVisibleOverFullscreen() const {
    return dockState_.value("alwaysOnTop").toBool(false);
}

void IcontraHost::start() {
    enabled_ = AppSettings().icontraEnabled();
    if (!enabled_) {
        setStatus("图标栏已禁用");
        return;
    }
    if (process_->state() != QProcess::NotRunning || restartTimer_->isActive())
        return;

    shuttingDown_ = false;
    setReady(false);
    token_ = secureToken();
    pipeName_ = QString("LightWallpaper.Icontra.%1.%2")
                    .arg(QCoreApplication::applicationPid())
                    .arg(token_.left(12));
    if (!openServer())
        return;
    startChild();
}

void IcontraHost::setEnabled(bool enabled) {
    AppSettings().setIcontraEnabled(enabled);
    enabled_ = enabled;
    if (enabled) {
        restartAttempts_ = 0;
        start();
    } else {
        shutdown();
    }
}

void IcontraHost::shutdown(int timeoutMs) {
    restartTimer_->stop();
    shuttingDown_ = true;
    setReady(false);
    if (process_->state() == QProcess::NotRunning) {
        closeServer();
        setStatus(enabled_ ? "图标栏已停止" : "图标栏已禁用");
        return;
    }
    if (authenticated_)
        send("shutdown", {}, requestId());
    shutdownTimer_->start(qMax(250, timeoutMs));
}

void IcontraHost::showDock() {
    send("show", {}, requestId());
}
void IcontraHost::hideDock() {
    send("hide", {}, requestId());
}
void IcontraHost::toggleDock() {
    send("toggle", {}, requestId());
}
void IcontraHost::updateDockSettings(const QJsonObject& settings) {
    send("updateSettings", settings, requestId());
}

void IcontraHost::addDockApplications(const QStringList& paths) {
    QJsonArray values;
    for (const auto& path : paths)
        values.append(path);
    send("addApplications", QJsonObject{{"paths", values}}, requestId());
}

void IcontraHost::removeDockApplication(const QString& id) {
    send("removeApplication", QJsonObject{{"id", id}}, requestId());
}

void IcontraHost::moveDockApplication(const QString& id, int destinationIndex) {
    send("moveApplication", QJsonObject{{"id", id}, {"destinationIndex", destinationIndex}},
         requestId());
}

void IcontraHost::resetDockPosition() {
    send("resetPosition", {}, requestId());
}

void IcontraHost::suspend() {
    wasVisibleBeforeSuspend_ = dockVisible_;
    if (wasVisibleBeforeSuspend_)
        hideDock();
}

void IcontraHost::resume() {
    if (wasVisibleBeforeSuspend_ && enabled_)
        showDock();
    wasVisibleBeforeSuspend_ = false;
}

void IcontraHost::setFullscreenActive(bool active) {
    if (hostFullscreenActive_ == active)
        return;
    hostFullscreenActive_ = active;
    if (active) {
        wasVisibleBeforeFullscreen_ = dockVisible_;
        // "Always on top" is a user override. Electron keeps the window in
        // the topmost z-order; the host must therefore not immediately undo
        // that choice with its normal fullscreen-hide policy.
        if (wasVisibleBeforeFullscreen_ && !keepVisibleOverFullscreen())
            hideDock();
        return;
    }

    if (wasVisibleBeforeFullscreen_ && enabled_)
        showDock();
    wasVisibleBeforeFullscreen_ = false;
}

void IcontraHost::recoverAfterExplorer() {
    if (authenticated_)
        send("getState", {}, requestId());
}

bool IcontraHost::openServer() {
    closeServer();
    // The name is per-launch and unpredictable, so removing a stale endpoint
    // cannot interfere with a different LightWallpaper instance.
    QLocalServer::removeServer(pipeName_);
    if (!server_->listen(pipeName_)) {
        const auto message = "Unable to listen for Icontra IPC: " + server_->errorString();
        Logger::error(message);
        emit warning(message);
        setStatus("图标栏 IPC 不可用");
        return false;
    }
    return true;
}

IcontraHost::LaunchCommand IcontraHost::resolveLaunchCommand() const {
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const auto installed = applicationDirectory.filePath("icontra/Icontra.exe");
    if (QFileInfo::exists(installed))
        return {installed, {}, QFileInfo(installed).absolutePath()};
    // The single installer is built by electron-builder. Its Electron Dock is
    // placed at the install root, while the Qt host lives in resources/lightwallpaper.
    const auto packagedDock = applicationDirectory.absoluteFilePath("../../Icontra.exe");
    if (QFileInfo::exists(packagedDock))
        return {packagedDock, {}, QFileInfo(packagedDock).absolutePath()};

    const QDir root(sourceRoot());
    // Keep each freshly built Electron runtime in its own directory.  That
    // avoids replacing a running executable while the desktop application is
    // being tested, and makes the newest managed runtime the development one.
    const auto optimizedRuntime =
        root.filePath("third_party/icontra-runtime-v8/win-unpacked/Icontra.exe");
    if (QFileInfo::exists(optimizedRuntime))
        return {optimizedRuntime, {}, QFileInfo(optimizedRuntime).absolutePath()};
    const auto latestRuntime =
        root.filePath("third_party/icontra-runtime-v6/win-unpacked/Icontra.exe");
    if (QFileInfo::exists(latestRuntime))
        return {latestRuntime, {}, QFileInfo(latestRuntime).absolutePath()};
    const auto v5Runtime = root.filePath("third_party/icontra-runtime-v5/win-unpacked/Icontra.exe");
    if (QFileInfo::exists(v5Runtime))
        return {v5Runtime, {}, QFileInfo(v5Runtime).absolutePath()};
    const auto previousRuntime =
        root.filePath("third_party/icontra-runtime-v3/win-unpacked/Icontra.exe");
    if (QFileInfo::exists(previousRuntime))
        return {previousRuntime, {}, QFileInfo(previousRuntime).absolutePath()};
    const auto refreshedRuntime =
        root.filePath("third_party/icontra-runtime-v2/win-unpacked/Icontra.exe");
    if (QFileInfo::exists(refreshedRuntime))
        return {refreshedRuntime, {}, QFileInfo(refreshedRuntime).absolutePath()};
    const auto unpacked = root.filePath("third_party/icontra-runtime/Icontra.exe");
    if (QFileInfo::exists(unpacked))
        return {unpacked, {}, QFileInfo(unpacked).absolutePath()};
    const auto unpackedBuild =
        root.filePath("third_party/icontra-runtime/win-unpacked/Icontra.exe");
    if (QFileInfo::exists(unpackedBuild))
        return {unpackedBuild, {}, QFileInfo(unpackedBuild).absolutePath()};

    const auto electron =
        root.filePath("third_party/icontra-source/node_modules/electron/dist/electron.exe");
    const auto source = root.filePath("third_party/icontra-source");
    if (QFileInfo::exists(electron) && QFileInfo::exists(source))
        return {electron, {source}, source};

    return {};
}

void IcontraHost::startChild() {
    const auto command = resolveLaunchCommand();
    if (command.program.isEmpty()) {
        const auto message =
            "Icontra runtime is unavailable. Build third_party/icontra-runtime first.";
        Logger::error(message);
        emit warning(message);
        setStatus("图标栏运行时缺失");
        return;
    }

    QStringList arguments = command.arguments;
    arguments << "--lightwallpaper-managed"
              << "--zlo-user-data=" + QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/icontra"
              << "--host-pipe=" + pipeName_ << "--host-token=" + token_
              << "--host-protocol-version=" + QString::number(ProtocolVersion);
    process_->setProgram(command.program);
    process_->setArguments(arguments);
    process_->setWorkingDirectory(command.workingDirectory);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    setStatus("图标栏正在启动");
    process_->start();
}

void IcontraHost::scheduleRestart(const QString& reason) {
    if (shuttingDown_ || !enabled_ || restartTimer_->isActive())
        return;
    if (++restartAttempts_ > 3) {
        const auto message = "Icontra restart limit reached: " + reason;
        Logger::error(message);
        emit warning(message);
        setStatus("图标栏启动失败");
        return;
    }
    const int delays[] = {1000, 3000, 8000};
    const int delay = delays[restartAttempts_ - 1];
    Logger::warning(QString("Restarting Icontra in %1 ms: %2").arg(delay).arg(reason));
    setStatus("图标栏正在恢复");
    restartTimer_->start(delay);
}

void IcontraHost::stopOwnedChild() {
    if (process_->state() == QProcess::NotRunning)
        return;
    process_->terminate();
    if (!process_->waitForFinished(500))
        process_->kill();
}

void IcontraHost::assignChildToJob() {
    if (!job_) {
        const HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (!job) {
            Logger::warning("Unable to create Icontra Job Object");
            return;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            Logger::warning("Unable to configure Icontra Job Object");
            CloseHandle(job);
            return;
        }
        job_ = job;
    }
    const HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                     static_cast<DWORD>(process_->processId()));
    if (!child) {
        Logger::warning("Unable to open Icontra child process for Job Object assignment");
        return;
    }
    if (!AssignProcessToJobObject(static_cast<HANDLE>(job_), child))
        Logger::warning("Unable to assign Icontra to Job Object");
    CloseHandle(child);
}

void IcontraHost::setReady(bool ready) {
    if (ready_ == ready)
        return;
    ready_ = ready;
    emit readyChanged(ready_);
}

void IcontraHost::setStatus(const QString& status) {
    emit statusChanged(status);
}

void IcontraHost::send(const QString& type, const QJsonObject& payload, const QString& requestId) {
    if (!authenticated_ || !socket_ || socket_->state() != QLocalSocket::ConnectedState)
        return;
    QJsonObject message{{"type", type},
                        {"requestId", requestId},
                        {"payload", payload},
                        {"protocolVersion", ProtocolVersion}};
    socket_->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    socket_->flush();
}

void IcontraHost::consumeSocket() {
    if (!socket_)
        return;
    receiveBuffer_ += QString::fromUtf8(socket_->readAll());
    while (true) {
        const int newline = receiveBuffer_.indexOf('\n');
        if (newline < 0)
            break;
        const auto line = receiveBuffer_.left(newline).trimmed();
        receiveBuffer_.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            Logger::warning("Ignored malformed Icontra IPC message");
            continue;
        }
        handleMessage(document.object());
    }
    if (receiveBuffer_.size() > 4 * 1024 * 1024) {
        Logger::warning("Icontra IPC receive buffer exceeded limit");
        socket_->disconnectFromServer();
    }
}

void IcontraHost::handleMessage(const QJsonObject& message) {
    const auto type = message.value("type").toString();
    const auto id = message.value("requestId").toString();
    const auto payload = message.value("payload").toObject();
    if (message.value("protocolVersion").toInt(-1) != ProtocolVersion) {
        Logger::warning("Rejected incompatible Icontra IPC protocol");
        if (socket_)
            socket_->disconnectFromServer();
        return;
    }
    if (!authenticated_) {
        if (type != "hello" || payload.value("token").toString() != token_) {
            Logger::warning("Rejected unauthenticated Icontra IPC client");
            if (socket_)
                socket_->disconnectFromServer();
            return;
        }
        authenticated_ = true;
        Logger::info("Icontra IPC client authenticated");
        send("initialize", QJsonObject{{"managed", true}}, id);
        return;
    }

    if (type == "ready") {
        restartAttempts_ = 0;
        setReady(true);
        setStatus("图标栏已连接");
        Logger::info("Icontra IPC ready received");
    } else if (type == "stateChanged") {
        const auto state = payload.value("state").toObject();
        if (!state.isEmpty()) {
            dockState_ = state;
            emit dockStateChanged(dockState_);
        }
        dockVisible_ = payload.value("visible").toBool(dockVisible_);
        if (hostFullscreenActive_ && dockVisible_ && !keepVisibleOverFullscreen()) {
            wasVisibleBeforeFullscreen_ = true;
            hideDock();
        } else if (hostFullscreenActive_ && keepVisibleOverFullscreen() &&
                   wasVisibleBeforeFullscreen_ && !dockVisible_) {
            // If the user enables the override while the Dock is hidden for
            // a fullscreen application, restore it immediately.
            showDock();
            wasVisibleBeforeFullscreen_ = false;
        }
    } else if (type == "fullscreenChanged") {
        const bool active = payload.value("active").toBool();
        if (fullscreenActive_ != active) {
            fullscreenActive_ = active;
            emit fullscreenChanged(active);
        }
    } else if (type == "warning") {
        const auto warningMessage = payload.value("message").toString("Icontra reported a warning");
        Logger::warning("Icontra IPC: " + warningMessage);
        emit warning(warningMessage);
    } else if (type == "exitRequested") {
        Logger::info("Icontra requested managed exit");
        setEnabled(false);
    } else if (type == "shutdownComplete") {
        // Keep the watchdog armed until the owned process has actually exited.
        // Acknowledging the message alone is not sufficient to prove cleanup.
        Logger::info("Icontra IPC shutdown acknowledgement received");
    } else {
        Logger::warning("Ignored unknown Icontra IPC message: " + type);
        send("warning", QJsonObject{{"message", "Unknown Dock message: " + type}}, id);
    }
}

void IcontraHost::closeServer() {
    if (socket_) {
        auto* currentSocket = socket_;
        socket_ = nullptr;
        currentSocket->disconnectFromServer();
        currentSocket->deleteLater();
    }
    authenticated_ = false;
    receiveBuffer_.clear();
    if (server_->isListening())
        server_->close();
    if (!pipeName_.isEmpty())
        QLocalServer::removeServer(pipeName_);
}
