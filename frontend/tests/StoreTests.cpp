#include "store/StoreService.h"
#include "store/DownloadManager.h"
#include "store/DemoServer.h"
#include "store/CredentialVault.h"
#include "library/WallpaperLibrary.h"
#include "ui/StorePage.h"
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QImage>
#include <QImageReader>
#include <QListWidget>
#include <QPushButton>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

class StoreTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qApp->setOrganizationName("ZloWallpaperTests");
        qApp->setApplicationName("case-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    void catalogAndAuthentication() {
        DemoServer demo; QVERIFY(demo.start());
        StoreService store; store.configure(demo.baseUrl(), true);
        QSignalSpy catalog(&store, &StoreService::catalogChanged);
        store.refreshCatalog(); QTRY_COMPARE_WITH_TIMEOUT(catalog.size(), 1, 6000);
        QCOMPARE(store.products().size(), 6);
        store.refreshCatalog("雾山"); QTRY_COMPARE(catalog.size(), 2); QCOMPARE(store.products().size(), 1);
        QSignalSpy errors(&store, &StoreService::error);
        store.login("demo", "wrong", false); QTRY_COMPARE(errors.size(), 1); QVERIFY(!store.loggedIn());
        store.login("demo", "Demo12345", false); QTRY_VERIFY(store.loggedIn()); QVERIFY(!store.accountKey().isEmpty());
        store.logout(); QVERIFY(!store.loggedIn()); QVERIFY(store.owned().isEmpty());
        QVERIFY(!ApiClient::safeUrl(QUrl("http://example.com/api/v1")));
        QVERIFY(!ApiClient::safeUrl(QUrl("https://user:password@example.com/api/v1")));
    }
    void supportedMediaDecoders() {
        const auto formats = QImageReader::supportedImageFormats();
        for (const auto& format : {QByteArray("png"), QByteArray("jpeg"), QByteArray("gif"), QByteArray("webp")})
            QVERIFY2(formats.contains(format), format.constData());
        QTemporaryDir directory;
        QImage original(64, 32, QImage::Format_RGB32); original.fill(QColor("#247c79"));
        const auto path = directory.path() + "/wallpaper.webp";
        QVERIFY(original.save(path, "WEBP"));
        QImage decoded(path); QCOMPARE(decoded.size(), original.size());
    }
    void unauthorizedDownloadAndFreeClaim() {
        DemoServer demo; QVERIFY(demo.start()); StoreService store; store.configure(demo.baseUrl(), true);
        QSignalSpy errors(&store, &StoreService::error), catalog(&store, &StoreService::catalogChanged), owned(&store, &StoreService::ownedChanged);
        store.refreshCatalog(); QTRY_VERIFY(!store.products().isEmpty()); const auto product = store.products()[0].toObject();
        store.requestDownload(product); QTRY_COMPARE(errors.size(), 1);
        store.login("demo", "Demo12345", false); QTRY_VERIFY(store.loggedIn());
        store.requestDownload(product); QTRY_COMPARE(errors.size(), 2); QVERIFY(!store.owns(product.value("id").toString()));
        QSignalSpy orders(&store, &StoreService::orderCreated);
        store.createOrder(product.value("id").toString()); QTRY_COMPARE(orders.size(), 1);
        QCOMPARE(orders.at(0).at(0).toJsonObject().value("status").toString(), "PAID");
        QTRY_VERIFY(store.owns(product.value("id").toString()));
        store.createOrder(product.value("id").toString()); QTRY_COMPARE(orders.size(), 2);
        QCOMPARE(orders.at(0).at(0).toJsonObject().value("id"), orders.at(1).at(0).toJsonObject().value("id"));
    }
    void paidOrderAndDownloadRoundTrip() {
        DemoServer demo; QVERIFY(demo.start()); StoreService store; store.configure(demo.baseUrl(), true);
        store.refreshCatalog(); QTRY_COMPARE(store.products().size(), 6);
        const auto product = store.products()[1].toObject(); store.login("demo", "Demo12345", false); QTRY_VERIFY(store.loggedIn());
        QSignalSpy orders(&store, &StoreService::orderCreated); store.createOrder(product.value("id").toString()); QTRY_COMPARE(orders.size(), 1);
        const auto order = orders.at(0).at(0).toJsonObject(); QCOMPARE(order.value("status").toString(), "PENDING");
        store.payTestOrder(order.value("id").toString()); QTRY_VERIFY(store.owns(product.value("id").toString()));
        QTemporaryDir directory; WallpaperLibrary library(nullptr, directory.path());
        const auto downloadRoot = directory.path() + "/wallpapers";
        DownloadManager downloads(&library, nullptr, downloadRoot);
        connect(&store, &StoreService::downloadAuthorized, &downloads, &DownloadManager::start);
        QSignalSpy imports(&downloads, &DownloadManager::imported);
        QSignalSpy states(&downloads, &DownloadManager::stateChanged);
        store.requestDownload(product);
        QTRY_VERIFY_WITH_TIMEOUT(imports.size() == 1 || downloads.state(product.value("id").toString()) == "failed", 10000);
        QVERIFY2(imports.size() == 1, states.isEmpty() ? "No download state" : qPrintable(states.last().at(2).toString()));
        QCOMPARE(library.entries().size(), 1); const auto entry = library.entries().first(); QVERIFY(QFile::exists(entry.path));
        QVERIFY(entry.path.startsWith(downloadRoot + "/"));
        QCOMPARE(DownloadManager::defaultDownloadDirectory(), QDir(qApp->applicationDirPath()).filePath("wallpapers"));
        QCOMPARE(entry.storeProductId, product.value("id").toString());
        QCOMPARE(entry.storeAccountKey, store.accountKey());
        WallpaperLibrary reloaded(nullptr, directory.path()); reloaded.load(); QCOMPARE(reloaded.entries().size(), 1);
        QCOMPARE(reloaded.entries()[0].storeProductId, entry.storeProductId);
        store.requestDownload(product); QTRY_COMPARE(imports.size(), 2); QCOMPARE(library.entries().size(), 1);
    }
    void corruptFileAndCancellation() {
        DemoServer demo; QVERIFY(demo.start()); StoreService store; store.configure(demo.baseUrl(), true);
        store.refreshCatalog(); QTRY_VERIFY(!store.products().isEmpty()); const auto product = store.products()[0].toObject();
        store.login("demo", "Demo12345", false); QTRY_VERIFY(store.loggedIn()); store.createOrder(product.value("id").toString()); QTRY_VERIFY(store.owns(product.value("id").toString()));
        QTemporaryDir directory; WallpaperLibrary library(nullptr, directory.path());
        DownloadManager downloads(&library, nullptr, directory.path() + "/wallpapers");
        connect(&store, &StoreService::downloadAuthorized, &downloads, &DownloadManager::start);
        demo.setCorruptDownloads(true); store.requestDownload(product);
        QTRY_COMPARE(downloads.state(product.value("id").toString()), "failed"); QVERIFY(library.entries().isEmpty());
        demo.setCorruptDownloads(false);
        connect(&downloads, &DownloadManager::stateChanged, &downloads, [&downloads](const QString& id, const QString& state, const QString&) {
            if (state == "downloading") QTimer::singleShot(0, &downloads, [&downloads, id] { downloads.cancel(id); });
        });
        store.requestDownload(product); QTRY_COMPARE(downloads.state(product.value("id").toString()), "canceled"); QVERIFY(library.entries().isEmpty());
    }
    void accountSwitchDoesNotReuseEntitlements() {
        DemoServer demo; QVERIFY(demo.start()); StoreService store; store.configure(demo.baseUrl(), true);
        store.refreshCatalog(); QTRY_VERIFY(!store.products().isEmpty()); const auto product = store.products()[0].toObject();
        store.login("demo", "Demo12345", false); QTRY_VERIFY(store.loggedIn()); const auto firstAccount = store.accountKey();
        store.createOrder(product.value("id").toString()); QTRY_VERIFY(store.owns(product.value("id").toString()));
        QSignalSpy messages(&store, &StoreService::message); store.registerAccount("tester", "Test123456"); QTRY_COMPARE(messages.size(), 1);
        store.login("tester", "Test123456", false); QTRY_VERIFY(store.loggedIn()); QVERIFY(firstAccount != store.accountKey());
        QTRY_VERIFY(!store.owns(product.value("id").toString()));
    }
    void libraryRollbackAndVault() {
        QTemporaryDir directory; QVERIFY(QDir().mkdir(directory.path() + "/library.json"));
        const auto filePath = directory.path() + "/file.png"; QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        WallpaperLibrary library(nullptr, directory.path()); WallpaperEntry entry; entry.id = "test"; entry.path = filePath;
        QVERIFY(!library.addPrepared(entry).ok); QVERIFY(library.entries().isEmpty());
        const QJsonObject credentials{{"baseUrl", "https://example.com/api/v1"}, {"refreshToken", "fixture-secret"}};
        QVERIFY(CredentialVault::write(credentials)); QCOMPARE(CredentialVault::read(), credentials);
        CredentialVault::clear(); QVERIFY(CredentialVault::read().isEmpty());
    }
    void storefrontUiWorkflow() {
        QTemporaryDir directory;
        WallpaperLibrary library(nullptr, directory.path());
        StorePage page(&library, nullptr, directory.path() + "/wallpapers");
        page.resize(1200, 720); page.show();
        QTest::qWait(100); // Let the initial real-endpoint configuration finish.
        auto findButton = [](QWidget* widget, const QString& text) -> QPushButton* {
            for (auto* b : widget->findChildren<QPushButton*>()) if (b->text() == text) return b;
            return nullptr;
        };
        auto* demo = findButton(&page, "进入演示模式"); QVERIFY(demo); demo->click();
        auto* gallery = page.findChild<QListWidget*>("storeGallery"); QVERIFY(gallery);
        QTRY_COMPARE(gallery->count(), 6);
        gallery->setCurrentRow(0);
        auto* service = page.findChild<StoreService*>(); QVERIFY(service);
        QTimer::singleShot(50, &page, [&page, findButton] {
            auto* dialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
            if (!dialog) return;
            const auto fields = dialog->findChildren<QLineEdit*>();
            if (fields.size() < 2) { dialog->reject(); return; }
            fields[0]->setText("demo"); fields[1]->setText("Demo12345");
            if (auto* login = findButton(dialog, "登录")) login->click(); else dialog->reject();
        });
        QTimer::singleShot(6000, &page, [] {
            if (auto* dialog = qobject_cast<QDialog*>(qApp->activeModalWidget())) dialog->reject();
        });
        auto* login = findButton(&page, "登录 / 注册"); QVERIFY(login); login->click();
        QVERIFY(service->loggedIn());
        auto* action = page.findChild<QPushButton*>("storePrimary"); QVERIFY(action);
        QTRY_COMPARE(action->text(), "免费领取"); QVERIFY(action->isEnabled()); action->click();
        QTRY_COMPARE(action->text(), "下载到本地库"); QVERIFY(action->isEnabled()); action->click();
        QTRY_COMPARE_WITH_TIMEOUT(library.entries().size(), 1, 10000);
        QTRY_COMPARE(action->text(), "应用到桌面");
        QSignalSpy apply(&page, &StorePage::applyLocalEntry); action->click();
        QCOMPARE(apply.size(), 1); QCOMPARE(apply.first().first().toString(), library.entries().first().id);
        auto* logout = findButton(&page, "退出登录"); QVERIFY(logout); logout->click();
        QVERIFY(!service->loggedIn()); QCOMPARE(library.entries().size(), 1);
    }
};
QTEST_MAIN(StoreTests)
#include "StoreTests.moc"
