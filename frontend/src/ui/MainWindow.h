#pragma once

#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QMainWindow>

class QCheckBox;
class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QJsonArray;
class WallpaperEngine;
class WallpaperLibrary;
class PreviewWidget;
class IcontraHost;
class TranslucentTBHost;
class StorePage;

class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(WallpaperLibrary* library, WallpaperEngine* engine, class MonitorManager* monitors,
               IcontraHost* icontra, TranslucentTBHost* translucentTb, QWidget* parent = nullptr);
    void raiseFromInstance();
    int displayedEntryCount() const;

  protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private slots:
    void rebuildList();
    void selectionChanged();
    void add();
    void remove();
    void apply();
    void stop();
    void restoreSystem();
    void openLocation();
    void showSettings();
    void showWallpaperSettings();
    void showWallpaperPage();
    void showDockPage();
    void updateDockPanel(const QJsonObject& state);
    void addDockApplications();
    void removeDockApplication();
    void moveDockApplicationUp();
    void moveDockApplicationDown();
    void sendDockSettings();
    void flushDockScale();
    void showMessage(const QString& title, const QString& text);

  private:
    const class WallpaperEntry* selected() const;
    void setupTray();
    void updateDetails(const class WallpaperEntry* entry);
    void setActivePage(int index);
    void updateDockControlsEnabled();
    void decodeDockIconsAsync(const QJsonArray& applications);

    WallpaperLibrary* library_ = nullptr;
    WallpaperEngine* engine_ = nullptr;
    IcontraHost* icontra_ = nullptr;
    TranslucentTBHost* translucentTb_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QPushButton* wallpaperTab_ = nullptr;
    QPushButton* dockTab_ = nullptr;
    QPushButton* storeTab_ = nullptr;
    StorePage* storePage_ = nullptr;

    QListWidget* list_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* libraryCount_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* type_ = nullptr;
    QLabel* details_ = nullptr;
    QLabel* path_ = nullptr;

    QLabel* dockStatus_ = nullptr;
    QLabel* dockAppCount_ = nullptr;
    QListWidget* dockApps_ = nullptr;
    QPushButton* dockAddButton_ = nullptr;
    QPushButton* dockRemoveButton_ = nullptr;
    QPushButton* dockUpButton_ = nullptr;
    QPushButton* dockDownButton_ = nullptr;
    QPushButton* dockVisibilityButton_ = nullptr;
    QCheckBox* dockEnabled_ = nullptr;
    QComboBox* dockOrientation_ = nullptr;
    QSlider* dockScale_ = nullptr;
    QLabel* dockScaleValue_ = nullptr;
    QCheckBox* dockHideButton_ = nullptr;
    QCheckBox* dockDesktopIconsButton_ = nullptr;
    QCheckBox* dockDesktopIconsHidden_ = nullptr;
    QCheckBox* dockAlwaysOnTop_ = nullptr;
    QCheckBox* taskbarTransparency_ = nullptr;
    QTimer* dockScaleTimer_ = nullptr;
    QHash<QString, QIcon> dockIconCache_;
    QString dockAppsFingerprint_;
    quint64 dockIconDecodeGeneration_ = 0;
    bool dockScaleDirty_ = false;
    bool updatingDockPanel_ = false;

    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayDockHideButtonAction_ = nullptr;
    QAction* trayDockDesktopButtonAction_ = nullptr;
    QAction* trayAutoStartAction_ = nullptr;
};
