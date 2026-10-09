#pragma once
#include <QJsonObject>
#include <QPixmap>
#include <QWidget>
#include <QHash>
#include <QSet>
#include <QNetworkAccessManager>
class WallpaperLibrary;
class StoreService;
class DownloadManager;
class DemoServer;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QListWidget;
class QProgressBar;
class QTreeWidget;

class StorePage final : public QWidget {
    Q_OBJECT
  public:
    explicit StorePage(WallpaperLibrary* library, QWidget* parent = nullptr);
  signals:
    void applyLocalEntry(const QString& id);
  private:
    void configure(bool demo, const QString& url = {});
    void reload();
    void renderGallery();
    void showDetail(const QJsonObject& product);
    void updateAction();
    void performAction();
    void showLogin();
    void showOrders();
    void showConnectionSettings();
    void loadCover(const QString& url);
    void paintCovers();
    WallpaperLibrary* library_;
    StoreService* store_;
    DownloadManager* downloads_;
    DemoServer* demoServer_;
    QLabel *environment_, *notice_, *account_, *count_, *detailCover_, *detailTitle_, *detailMeta_, *detailDescription_, *detailPrice_, *pageLabel_;
    QLineEdit* search_;
    QComboBox* category_;
    QPushButton *login_, *logout_, *action_, *cancel_, *ownedButton_, *previous_, *next_;
    QListWidget* gallery_;
    QProgressBar* progress_;
    QTreeWidget* tasks_;
    QNetworkAccessManager covers_;
    QHash<QString, QPixmap> coverCache_;
    QSet<QString> pendingCovers_;
    QJsonObject selected_;
    QString sessionAccountKey_;
    bool ownedOnly_ = false;
    bool actionPending_ = false;
    int page_ = 1, total_ = 0;
};
