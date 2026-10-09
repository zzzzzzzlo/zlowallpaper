#include "ui/StorePage.h"
#include "store/StoreService.h"
#include "store/DownloadManager.h"
#include "store/DemoServer.h"
#include "library/WallpaperLibrary.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QLabel* label(const QString& text, const QString& name = {}) {
    auto* value = new QLabel(text);
    value->setTextFormat(Qt::PlainText);
    value->setObjectName(name);
    return value;
}
QString money(int cents) { return cents == 0 ? "免费领取" : "¥ " + QString::number(cents / 100.0, 'f', 2); }
QPushButton* button(const QString& text, const QString& name = {}) {
    auto* value = new QPushButton(text); value->setObjectName(name); return value;
}
}
StorePage::StorePage(WallpaperLibrary* library, QWidget* parent)
    : QWidget(parent), library_(library), store_(new StoreService(this)),
      downloads_(new DownloadManager(library, this)), demoServer_(new DemoServer(this)) {
    setObjectName("storePage");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
      QWidget#storePage { background:#f2f6f7; color:#183e43; font-family:"Microsoft YaHei UI"; }
      QWidget#storePage QLabel { background:transparent; border:none; color:#536d71; }
      QWidget#storePage QLabel#storeHeading { font-family:"Microsoft YaHei UI"; font-size:28px; font-weight:600; color:#153c41; }
      QWidget#storePage QLabel#storeTitle { font-size:19px; font-weight:600; color:#153c41; }
      QWidget#storePage QLabel#storePrice { font-family:"Bahnschrift"; font-size:24px; color:#166966; }
      QWidget#storePage QLabel#storeNotice { background:#e2edef; border-radius:8px; padding:9px; color:#355d63; }
      QWidget#storePage QLineEdit, QWidget#storePage QComboBox { background:white; color:#183e43; border:1px solid #c8d9dc; border-radius:8px; padding:8px; }
      QWidget#storePage QLineEdit:focus, QWidget#storePage QComboBox:focus { border:2px solid #247c79; }
      QWidget#storePage QPushButton { min-height:32px; color:#305d63; background:white; border:1px solid #bfd2d6; border-radius:8px; padding:0 13px; }
      QWidget#storePage QPushButton:hover { background:#e9f3f3; }
      QWidget#storePage QPushButton:focus { border:2px solid #247c79; }
      QWidget#storePage QPushButton:checked, QWidget#storePage QPushButton#storePrimary { color:white; background:#247c79; border-color:#247c79; }
      QWidget#storePage QPushButton:disabled { color:#8fa6a9; background:#eef2f3; border-color:#dbe4e5; }
      QWidget#storePage QFrame#storeDetail { background:white; border:1px solid #d4e1e3; border-radius:12px; }
      QWidget#storePage QListWidget { background:transparent; border:none; outline:0; }
      QWidget#storePage QListWidget::item { border:1px solid #d2e0e2; border-radius:10px; background:white; }
      QWidget#storePage QListWidget::item:selected { border:2px solid #247c79; background:#f7fcfc; }
      QWidget#storePage QProgressBar { border:1px solid #cfdddd; border-radius:5px; background:#edf3f4; text-align:center; color:#204f55; }
      QWidget#storePage QProgressBar::chunk { background:#70bab0; }
      QWidget#storePage QTreeWidget { background:white; color:#36565c; border:1px solid #d4e1e3; border-radius:6px; }
    )");
    auto* root = new QVBoxLayout(this); root->setContentsMargins(14, 8, 14, 8); root->setSpacing(12);
    auto* heading = new QHBoxLayout;
    auto* intro = new QVBoxLayout;
    intro->addWidget(label("把喜欢的风景带到桌面", "storeHeading"));
    intro->addWidget(label("WALLPAPER COLLECTION  /  浏览、收藏于本机，随时换一片风景"));
    heading->addLayout(intro); heading->addStretch();
    account_ = label("未登录"); login_ = button("登录 / 注册"); logout_ = button("退出登录");
    logout_->hide(); heading->addWidget(account_); heading->addWidget(login_); heading->addWidget(logout_);
    root->addLayout(heading);
    auto* environmentBar = new QHBoxLayout;
    environment_ = label("真实接口模式"); environmentBar->addWidget(environment_, 1);
    auto* connection = button("连接设置"); auto* demo = button("进入演示模式"); auto* orders = button("我的订单");
    environmentBar->addWidget(connection); environmentBar->addWidget(demo); environmentBar->addWidget(orders);
    root->addLayout(environmentBar);
    notice_ = label("浏览商城无需登录；本地壁纸和桌面美化功能始终可以使用。", "storeNotice");
    notice_->setWordWrap(true); root->addWidget(notice_);
    auto* tools = new QHBoxLayout;
    search_ = new QLineEdit; search_->setPlaceholderText("搜索壁纸标题"); search_->setClearButtonEnabled(true);
    category_ = new QComboBox; category_->addItem("全部分类", ""); category_->setMinimumWidth(130);
    ownedButton_ = button("我的已购"); ownedButton_->setCheckable(true);
    auto* refresh = button("刷新"); count_ = label("等待加载");
    tools->addWidget(search_, 1); tools->addWidget(category_); tools->addWidget(ownedButton_); tools->addWidget(refresh); tools->addWidget(count_);
    root->addLayout(tools);
    auto* split = new QSplitter;
    gallery_ = new QListWidget; gallery_->setObjectName("storeGallery");
    gallery_->setViewMode(QListView::IconMode); gallery_->setResizeMode(QListView::Adjust); gallery_->setMovement(QListView::Static);
    gallery_->setWrapping(true); gallery_->setSpacing(12); gallery_->setGridSize({262, 240});
    gallery_->setMinimumWidth(300); split->addWidget(gallery_);
    auto* detail = new QFrame; detail->setObjectName("storeDetail"); detail->setMinimumWidth(280); detail->setMaximumWidth(350);
    auto* info = new QVBoxLayout(detail); info->setContentsMargins(16, 16, 16, 16); info->setSpacing(10);
    detailCover_ = label("选择壁纸查看详情"); detailCover_->setAlignment(Qt::AlignCenter); detailCover_->setFixedHeight(164);
    detailTitle_ = label("选择一张壁纸", "storeTitle"); detailTitle_->setWordWrap(true);
    detailMeta_ = label("分辨率 · 格式 · 资源版本"); detailMeta_->setWordWrap(true);
    detailDescription_ = label("点击左侧壁纸，查看详细信息及购买、下载状态。"); detailDescription_->setWordWrap(true);
    detailPrice_ = label({}, "storePrice");
    action_ = button("选择壁纸", "storePrimary"); action_->setEnabled(false); cancel_ = button("取消下载"); cancel_->hide();
    progress_ = new QProgressBar; progress_->hide();
    info->addWidget(detailCover_); info->addWidget(detailTitle_); info->addWidget(detailMeta_); info->addWidget(detailDescription_);
    info->addStretch(); info->addWidget(detailPrice_); info->addWidget(progress_); info->addWidget(action_); info->addWidget(cancel_);
    split->addWidget(detail); split->setStretchFactor(0, 1); root->addWidget(split, 1);
    auto* pagination = new QHBoxLayout;
    previous_ = button("上一页"); next_ = button("下一页"); pageLabel_ = label("第 1 页");
    pagination->addStretch(); pagination->addWidget(previous_); pagination->addWidget(pageLabel_); pagination->addWidget(next_); root->addLayout(pagination);
    tasks_ = new QTreeWidget; tasks_->setHeaderLabels({"下载任务", "状态", "进度"}); tasks_->setRootIsDecorated(false); tasks_->setMaximumHeight(104);
    tasks_->header()->setSectionResizeMode(0, QHeaderView::Stretch); tasks_->hide(); root->addWidget(tasks_);
    auto* delay = new QTimer(this); delay->setSingleShot(true); delay->setInterval(300);
    connect(search_, &QLineEdit::textChanged, this, [this, delay] { page_ = 1; delay->start(); });
    connect(delay, &QTimer::timeout, this, &StorePage::reload);
    connect(category_, &QComboBox::currentIndexChanged, this, [this] { page_ = 1; reload(); });
    connect(refresh, &QPushButton::clicked, this, [this] { if (ownedOnly_) store_->syncOwned(); reload(); });
    connect(previous_, &QPushButton::clicked, this, [this] { if (page_ > 1) --page_; reload(); });
    connect(next_, &QPushButton::clicked, this, [this] { ++page_; reload(); });
    connect(ownedButton_, &QPushButton::toggled, this, [this](bool value) {
        ownedOnly_ = value; page_ = 1;
        if (value && !store_->loggedIn()) { showLogin(); }
        if (value && store_->loggedIn()) store_->syncOwned();
        reload();
    });
    connect(gallery_, &QListWidget::itemSelectionChanged, this, [this] {
        if (auto* item = gallery_->currentItem()) {
            showDetail(item->data(Qt::UserRole).toJsonObject());
            store_->fetchDetail(selected_.value("id").toString());
        }
    });
    connect(store_, &StoreService::detailLoaded, this, [this](const QJsonObject& p) {
        if (p.value("id") == selected_.value("id")) showDetail(p);
    });
    connect(login_, &QPushButton::clicked, this, &StorePage::showLogin);
    connect(logout_, &QPushButton::clicked, store_, &StoreService::logout);
    connect(orders, &QPushButton::clicked, this, &StorePage::showOrders);
    connect(connection, &QPushButton::clicked, this, &StorePage::showConnectionSettings);
    connect(demo, &QPushButton::clicked, this, [this] { configure(true); });
    connect(action_, &QPushButton::clicked, this, &StorePage::performAction);
    connect(cancel_, &QPushButton::clicked, this, [this] { downloads_->cancel(selected_.value("id").toString()); });
    connect(store_, &StoreService::sessionChanged, this, [this] {
        const auto accountKey = store_->accountKey();
        if (accountKey != sessionAccountKey_) {
            downloads_->resetStates();
            tasks_->clear(); tasks_->hide();
            sessionAccountKey_ = accountKey;
        }
        actionPending_ = false;
        account_->setText(store_->loggedIn() ? store_->user().value("displayName").toString() : "未登录");
        login_->setVisible(!store_->loggedIn()); logout_->setVisible(store_->loggedIn());
        if (ownedOnly_) renderGallery(); updateAction();
    });
    connect(store_, &StoreService::catalogChanged, this, [this](const QJsonArray&, int page, int total) { page_ = page; total_ = total; if (!ownedOnly_) renderGallery(); });
    connect(store_, &StoreService::categoriesChanged, this, [this](const QJsonArray& categories) {
        const auto current = category_->currentData(); QSignalBlocker blocked(category_);
        category_->clear(); category_->addItem("全部分类", "");
        for (const auto& v : categories) { const auto p = v.toObject(); category_->addItem(p.value("name").toString(), p.value("id").toString()); }
        const auto index = category_->findData(current); category_->setCurrentIndex(qMax(0, index));
    });
    connect(store_, &StoreService::ownedChanged, this, [this] { if (ownedOnly_) renderGallery(); updateAction(); });
    connect(store_, &StoreService::error, this, [this](const QString& value) { actionPending_ = false; notice_->setText(value); updateAction(); });
    connect(store_, &StoreService::message, notice_, &QLabel::setText);
    connect(store_, &StoreService::orderCreated, this, [this](const QJsonObject& order) {
        actionPending_ = false;
        if (order.value("status").toString() == "PAID") { notice_->setText("已领取，刷新已购后可以下载。"); updateAction(); return; }
        if (order.value("testPaymentEnabled").toBool()) {
            if (QMessageBox::question(this, "测试订单（不扣款）", "订单金额 " + money(order.value("amountCents").toInt()) + "。\n当前后端允许模拟支付，不会产生真实扣款。是否完成测试订单？") == QMessageBox::Yes)
                store_->payTestOrder(order.value("id").toString());
        } else notice_->setText("订单已创建。当前版本未接入真实收银台，请在订单页查看状态。");
        updateAction();
    });
    connect(store_, &StoreService::downloadAuthorized, downloads_, &DownloadManager::start);
    connect(downloads_, &DownloadManager::stateChanged, this, [this](const QString& id, const QString& state, const QString& value) {
        tasks_->show(); QTreeWidgetItem* row = nullptr;
        for (int i = 0; i < tasks_->topLevelItemCount(); ++i) if (tasks_->topLevelItem(i)->data(0, Qt::UserRole).toString() == id) row = tasks_->topLevelItem(i);
        if (!row) { row = new QTreeWidgetItem(tasks_); row->setData(0, Qt::UserRole, id); row->setText(0, id); }
        row->setText(1, value);
        if (state == "completed") row->setText(2, "100%");
        if (selected_.value("id").toString() == id) { actionPending_ = false; notice_->setText(value); updateAction(); }
    });
    connect(downloads_, &DownloadManager::progress, this, [this](const QString& id, qint64 received, qint64 total) {
        const int percent = total > 0 ? int(received * 100 / total) : 0;
        for (int i = 0; i < tasks_->topLevelItemCount(); ++i) if (tasks_->topLevelItem(i)->data(0, Qt::UserRole).toString() == id) tasks_->topLevelItem(i)->setText(2, QString::number(percent) + "%");
        if (selected_.value("id").toString() == id) progress_->setValue(percent);
    });
    connect(downloads_, &DownloadManager::imported, this, [this] { updateAction(); });
    connect(library_, &WallpaperLibrary::changed, this, &StorePage::updateAction);
    covers_.setTransferTimeout(12000);
    const bool demoMode = qApp->arguments().contains("--demo");
    QString apiUrl;
    const int argument = qApp->arguments().indexOf("--api");
    if (argument >= 0 && argument + 1 < qApp->arguments().size()) apiUrl = qApp->arguments().at(argument + 1);
    QTimer::singleShot(0, this, [this, demoMode, apiUrl] { configure(demoMode, apiUrl); });
}
void StorePage::configure(bool demo, const QString& url) {
    downloads_->resetStates(); selected_ = {}; actionPending_ = false; page_ = 1;
    QUrl endpoint;
    if (demo) {
        if (!demoServer_->start()) { notice_->setText("无法启动本机演示服务器"); return; }
        endpoint = demoServer_->baseUrl();
    } else {
        const auto saved = QSettings("ZloWallpaper", "ZloWallpaper").value("store/baseUrl", "http://127.0.0.1:8080/api/v1").toString();
        endpoint = QUrl(url.isEmpty() ? saved : url);
    }
    store_->configure(endpoint, demo);
    environment_->setText(demo ? "演示环境 · 本机样例数据 · 不产生真实扣款" : "真实接口 · " + endpoint.toString());
    notice_->setText(demo ? "演示账号 demo / Demo12345；也可以注册测试账号。演示数据仅在本次运行期间保留。" : "连接失败时可检查后端地址，或进入演示模式体验完整流程。");
    showDetail({}); reload(); if (!demo) store_->restoreSession();
}
void StorePage::reload() {
    if (ownedOnly_) { renderGallery(); return; }
    count_->setText("正在加载…");
    store_->refreshCatalog(search_->text().trimmed(), category_->currentData().toString(), page_);
}
void StorePage::renderGallery() {
    const auto selectedId = selected_.value("id").toString();
    QSignalBlocker blocked(gallery_); gallery_->clear();
    auto items = ownedOnly_ ? store_->owned() : store_->products();
    bool selectionFound = false;
    for (const auto& value : items) {
        const auto p = value.toObject();
        if (ownedOnly_ && (!p.value("title").toString().contains(search_->text().trimmed(), Qt::CaseInsensitive) ||
            (!category_->currentData().toString().isEmpty() && category_->currentData().toString() != p.value("categoryId").toString()))) continue;
        auto* item = new QListWidgetItem(gallery_); item->setSizeHint({250, 224}); item->setData(Qt::UserRole, p);
        auto* card = new QWidget; auto* layout = new QVBoxLayout(card); layout->setContentsMargins(10, 10, 10, 10); layout->setSpacing(6);
        auto* cover = label("加载封面…"); cover->setObjectName("storeCover"); cover->setProperty("coverUrl", p.value("coverUrl").toString());
        cover->setAlignment(Qt::AlignCenter); cover->setFixedSize(228, 128); cover->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(cover); auto* title = label(p.value("title").toString()); title->setAttribute(Qt::WA_TransparentForMouseEvents); layout->addWidget(title);
        auto* meta = label(p.value("categoryName").toString() + " · " + p.value("type").toString() + " · " + money(p.value("priceCents").toInt())); meta->setAttribute(Qt::WA_TransparentForMouseEvents); layout->addWidget(meta);
        gallery_->setItemWidget(item, card);
        loadCover(p.value("coverUrl").toString());
        if (p.value("id").toString() == selectedId) { gallery_->setCurrentItem(item); selectionFound = true; }
    }
    count_->setText(ownedOnly_ ? "已购 " + QString::number(gallery_->count()) + " 张" : "共 " + QString::number(total_) + " 张");
    pageLabel_->setText(ownedOnly_ ? "购买权益与本机下载状态分别管理" : "第 " + QString::number(page_) + " / " + QString::number(qMax(1, (total_ + 11) / 12)) + " 页");
    previous_->setEnabled(!ownedOnly_ && page_ > 1); next_->setEnabled(!ownedOnly_ && page_ * 12 < total_);
    if (!selectionFound && !selectedId.isEmpty()) showDetail({});
    if (gallery_->count() == 0) {
        auto* empty = new QListWidgetItem(ownedOnly_ ? "还没有已购壁纸\n先登录并领取或购买一张" : "没有找到壁纸\n调整筛选或检查后端连接", gallery_);
        empty->setFlags(Qt::NoItemFlags); empty->setSizeHint({260, 160});
    }
    paintCovers();
}
void StorePage::showDetail(const QJsonObject& p) {
    if (p.value("id") != selected_.value("id")) actionPending_ = false;
    selected_ = p;
    detailTitle_->setText(p.value("title").toString("选择一张壁纸"));
    detailDescription_->setText(p.value("description").toString("选择左侧商品以查看购买和下载状态。"));
    detailMeta_->setText(p.isEmpty() ? QString{} : QString("%1 × %2  ·  %3\n%4  ·  版本 %5\n作者：%6")
        .arg(p.value("width").toInt()).arg(p.value("height").toInt()).arg(p.value("type").toString(), p.value("categoryName").toString(), p.value("resourceVersion").toString(), p.value("creator").toString()));
    detailPrice_->setText(p.isEmpty() ? QString{} : money(p.value("priceCents").toInt()));
    detailCover_->setProperty("coverUrl", p.value("coverUrl").toString()); detailCover_->setText(p.isEmpty() ? "壁纸详情" : "加载封面…");
    loadCover(p.value("coverUrl").toString()); paintCovers(); updateAction();
}
void StorePage::updateAction() {
    const auto id = selected_.value("id").toString(); const bool busy = downloads_->busy(id);
    action_->setEnabled(!id.isEmpty() && !busy && !actionPending_);
    cancel_->setVisible(busy); progress_->setVisible(busy);
    if (id.isEmpty()) { action_->setText("选择壁纸"); return; }
    if (busy) { action_->setText("正在下载 / 入库"); return; }
    if (actionPending_) { action_->setText("正在处理…"); return; }
    if (!store_->loggedIn()) { action_->setText("登录后领取 / 购买"); return; }
    if (library_->findStoreEntry(id, selected_.value("resourceVersion").toString(), store_->accountKey())) { action_->setText("应用到桌面"); return; }
    action_->setText(store_->owns(id) ? "下载到本地库" : (selected_.value("priceCents").toInt() == 0 ? "免费领取" : "购买壁纸"));
}
void StorePage::performAction() {
    if (selected_.isEmpty()) return;
    if (!store_->loggedIn()) { showLogin(); return; }
    const auto id = selected_.value("id").toString();
    if (const auto* entry = library_->findStoreEntry(id, selected_.value("resourceVersion").toString(), store_->accountKey())) { emit applyLocalEntry(entry->id); return; }
    actionPending_ = true; updateAction();
    if (store_->owns(id)) store_->requestDownload(selected_); else store_->createOrder(id);
}
void StorePage::showLogin() {
    QDialog dialog(this); dialog.setWindowTitle("ZloWallpaper · 登录 / 注册"); dialog.setMinimumWidth(410);
    auto* form = new QFormLayout(&dialog);
    auto* username = new QLineEdit; auto* password = new QLineEdit; password->setEchoMode(QLineEdit::Password);
    username->setMaxLength(32); password->setMaxLength(128);
    auto* remember = new QCheckBox("安全保存登录状态（Windows 加密）"); remember->setChecked(true); remember->setEnabled(!store_->demo());
    auto* feedback = label(store_->demo() ? "演示账号：demo / Demo12345" : "注册用户名 3–32 字符，密码至少 8 字符"); feedback->setWordWrap(true);
    auto* login = button("登录", "storePrimary"); auto* registration = button("注册新账号");
    form->addRow(feedback); form->addRow("用户名", username); form->addRow("密码", password); form->addRow(remember); form->addRow(login, registration);
    connect(login, &QPushButton::clicked, &dialog, [=] {
        if (username->text().trimmed().isEmpty() || password->text().isEmpty()) { feedback->setText("请输入用户名和密码"); return; }
        login->setEnabled(false); registration->setEnabled(false); feedback->setText("正在登录…");
        store_->login(username->text().trimmed(), password->text(), remember->isChecked());
    });
    connect(registration, &QPushButton::clicked, &dialog, [=] {
        if (username->text().trimmed().size() < 3 || password->text().size() < 8) { feedback->setText("用户名至少 3 字符，密码至少 8 字符"); return; }
        login->setEnabled(false); registration->setEnabled(false);
        store_->registerAccount(username->text().trimmed(), password->text());
    });
    connect(store_, &StoreService::sessionChanged, &dialog, [this, &dialog, password] { if (store_->loggedIn()) { password->clear(); dialog.accept(); } });
    connect(store_, &StoreService::error, &dialog, [=](const QString& text) { feedback->setText(text); login->setEnabled(true); registration->setEnabled(true); });
    connect(store_, &StoreService::message, &dialog, [=](const QString& text) { feedback->setText(text); login->setEnabled(true); registration->setEnabled(true); });
    dialog.exec();
}
void StorePage::showOrders() {
    if (!store_->loggedIn()) { showLogin(); if (!store_->loggedIn()) return; }
    QDialog dialog(this); dialog.setWindowTitle("我的订单"); dialog.resize(760, 440);
    auto* layout = new QVBoxLayout(&dialog); auto* table = new QTableWidget(0, 4); table->setHorizontalHeaderLabels({"壁纸", "金额", "状态", "操作"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* status = label("正在读取订单…"); auto* refresh = button("刷新订单"); layout->addWidget(status); layout->addWidget(table, 1); layout->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, store_, &StoreService::fetchOrders);
    connect(store_, &StoreService::error, &dialog, [status](const QString& value) { status->setText(value); });
    connect(store_, &StoreService::ordersLoaded, &dialog, [this, table, status, &dialog](const QJsonArray& values) {
        table->setRowCount(values.size()); status->setText(values.isEmpty() ? "还没有订单，去商城挑选一张壁纸吧。" : "测试付款不会产生真实扣款。");
        for (int i = 0; i < values.size(); ++i) {
            table->removeCellWidget(i, 3);
            const auto order = values[i].toObject(); table->setItem(i, 0, new QTableWidgetItem(order.value("title").toString()));
            table->setItem(i, 1, new QTableWidgetItem(money(order.value("amountCents").toInt()))); table->setItem(i, 2, new QTableWidgetItem(order.value("status").toString()));
            if (order.value("status").toString() == "PENDING" && order.value("testPaymentEnabled").toBool()) {
                auto* pay = button("完成测试付款"); table->setCellWidget(i, 3, pay);
                connect(pay, &QPushButton::clicked, &dialog, [this, order, pay] {
                    pay->setEnabled(false); store_->payTestOrder(order.value("id").toString());
                });
            }
        }
    });
    store_->fetchOrders(); dialog.exec();
}
void StorePage::showConnectionSettings() {
    bool ok = false;
    const auto text = QInputDialog::getText(this, "后端连接设置", "API 根地址（包含 /api/v1）：", QLineEdit::Normal,
        QSettings("ZloWallpaper", "ZloWallpaper").value("store/baseUrl", "http://127.0.0.1:8080/api/v1").toString(), &ok).trimmed();
    if (!ok) return;
    const QUrl url(text);
    if (!ApiClient::safeUrl(url) || url.path() != "/api/v1" || url.hasQuery() || url.hasFragment()) { notice_->setText("请输入 HTTPS 或本机 HTTP 地址，路径为 /api/v1，例如 http://127.0.0.1:8080/api/v1"); return; }
    store_->logout(); QSettings("ZloWallpaper", "ZloWallpaper").setValue("store/baseUrl", text); configure(false, text);
}
void StorePage::loadCover(const QString& url) {
    if (url.isEmpty() || coverCache_.contains(url) || pendingCovers_.contains(url) || !ApiClient::safeUrl(QUrl(url))) return;
    pendingCovers_.insert(url); QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = covers_.get(request);
    connect(reply, &QIODevice::readyRead, this, [reply] { if (reply->bytesAvailable() > 4 * 1024 * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        pendingCovers_.remove(url);
        QPixmap pixmap;
        if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) pixmap.loadFromData(reply->readAll());
        if (!pixmap.isNull()) { if (coverCache_.size() > 96) coverCache_.clear(); coverCache_[url] = pixmap; }
        reply->deleteLater(); paintCovers();
    });
}
void StorePage::paintCovers() {
    auto labels = gallery_->findChildren<QLabel*>("storeCover"); labels.append(detailCover_);
    for (auto* cover : labels) {
        const auto url = cover->property("coverUrl").toString();
        if (coverCache_.contains(url)) cover->setPixmap(coverCache_[url].scaled(cover->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        else if (!pendingCovers_.contains(url)) cover->setText(url.isEmpty() ? "壁纸详情" : "暂无封面");
    }
}
