#pragma once
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <windows.h>

// A packaged IDE may redirect logical C: AppData to a physical D: directory.
// Resolve the directory first so QSaveFile's native rename stays on that volume.
// Keep QSaveFile atomicity: no direct-write fallback or cross-volume copying.
class AtomicFile : public QSaveFile {
  public:
    explicit AtomicFile(const QString& destination) : QSaveFile(physicalPath(destination)) {}
  private:
    static QString physicalPath(const QString& destination) {
        const QFileInfo info(destination);
        const auto parent = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
        HANDLE directory = CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (directory == INVALID_HANDLE_VALUE) return info.absoluteFilePath();
        const DWORD needed = GetFinalPathNameByHandleW(directory, nullptr, 0, FILE_NAME_NORMALIZED);
        std::wstring resolved(needed, L'\0');
        const DWORD count = needed ? GetFinalPathNameByHandleW(directory, resolved.data(), needed, FILE_NAME_NORMALIZED) : 0;
        CloseHandle(directory);
        if (!count || count >= needed) return info.absoluteFilePath();
        resolved.resize(count);
        auto path = QString::fromStdWString(resolved);
        // QSaveFile treats a colon after index 1 as an alternate data stream;
        // feed it an ordinary drive path, not a GetFinalPathName long prefix.
        if (path.startsWith("\\\\?\\UNC\\")) path = "\\\\" + path.mid(8);
        else if (path.startsWith("\\\\?\\")) path.remove(0, 4);
        return QDir::fromNativeSeparators(path) + "/" + info.fileName();
    }
};
