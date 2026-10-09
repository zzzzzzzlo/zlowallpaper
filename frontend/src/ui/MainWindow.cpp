#include "ui/MainWindow.h"
#include "ui/StorePage.h"

#include "config/AppSettings.h"
#include "icontra/IcontraHost.h"
#include "library/WallpaperEntry.h"
#include "library/WallpaperLibrary.h"
#include "system/AutoStartManager.h"
#include "system/TranslucentTBHost.h"
#include "ui/PreviewWidget.h"
#include "ui/SettingsDialog.h"
#include "wallpaper/WallpaperEngine.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <windows.h>

namespace {
QString mediaFilter() {
    return "媒体文件 (*.jpg *.jpeg *.png *.bmp *.webp *.gif *.mp4 *.webm *.mov *.avi)";
}

QString dockApplicationFilter() {
    return "应用与快捷方式 (*.exe *.lnk *.url *.appref-ms *.bat *.cmd)";
}

QImage imageFromDataUrl(const QString& dataUrl) {
    const int separator = dataUrl.indexOf(',');
    if (separator < 0)
        return {};
    QImage image;
    if (!image.loadFromData(QByteArray::fromBase64(dataUrl.mid(separator + 1).toUtf8())))
        return {};
    return image;
}

class WallpaperCard final : public QFrame {
  public:
    explicit WallpaperCard(const WallpaperEntry& entry, QWidget* parent = nullptr)
        : QFrame(parent) {
        setObjectName("wallpaperCard");
        setProperty("selected", false);
        setMinimumSize(166, 154);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(5, 5, 5, 7);
        layout->setSpacing(5);

        auto* cover = new QLabel;
        cover->setObjectName("cardCover");
        cover->setFixedHeight(94);
        cover->setAlignment(Qt::AlignCenter);
        const QPixmap thumbnail(entry.thumbnailPath);
        if (thumbnail.isNull())
            cover->setText("无预览");
        else
            cover->setPixmap(thumbnail.scaled(QSize(154, 94), Qt::KeepAspectRatioByExpanding,
                                              Qt::SmoothTransformation));
        layout->addWidget(cover);

        auto* title =
            new QLabel(QFontMetrics(font()).elidedText(entry.fileName, Qt::ElideRight, 148));
        title->setObjectName("cardTitle");
        title->setAlignment(Qt::AlignCenter);
        title->setToolTip(entry.fileName);
        layout->addWidget(title);
        auto* type = new QLabel(WallpaperEntry::typeName(entry.type));
        type->setObjectName("cardType");
        type->setAlignment(Qt::AlignCenter);
        layout->addWidget(type);
    }
    void setSelected(bool selected) {
        if (property("selected").toBool() == selected)
            return;
        setProperty("selected", selected);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
};
} // namespace

MainWindow::MainWindow(WallpaperLibrary* library, WallpaperEngine* engine, MonitorManager*,
                       IcontraHost* icontra, TranslucentTBHost* translucentTb, QWidget* parent)
    : QMainWindow(parent), library_(library), engine_(engine), icontra_(icontra),
      translucentTb_(translucentTb) {
    setWindowTitle("ZloWallpaper");
    setWindowIcon(QApplication::windowIcon());
    resize(1280, 760);
    setMinimumSize(1040, 640);

    auto* root = new QWidget;
    root->setObjectName("root");
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(22, 18, 22, 22);
    layout->setSpacing(16);

    auto* header = new QHBoxLayout;
    auto* brand = new QVBoxLayout;
    brand->setSpacing(1);
    auto* brandName = new QLabel("ZLOWALLPAPER");
    brandName->setObjectName("brandName");
    auto* brandCaption = new QLabel("壁纸商城与桌面美化");
    brandCaption->setObjectName("brandCaption");
    brand->addWidget(brandName);
    brand->addWidget(brandCaption);
    header->addLayout(brand);
    header->addSpacing(36);
    wallpaperTab_ = new QPushButton("壁纸库");
    wallpaperTab_->setObjectName("pageTab");
    wallpaperTab_->setCheckable(true);
    dockTab_ = new QPushButton("图标栏");
    dockTab_->setObjectName("pageTab");
    dockTab_->setCheckable(true);
    header->addWidget(wallpaperTab_);
    header->addWidget(dockTab_);
    storeTab_ = new QPushButton("壁纸商城");
    storeTab_->setObjectName("pageTab");
    storeTab_->setCheckable(true);
    header->addWidget(storeTab_);
    header->addStretch();
    auto* globalSettingsButton = new QToolButton;
    globalSettingsButton->setObjectName("settingsButton");
    globalSettingsButton->setText("⚙");
    globalSettingsButton->setToolTip("全局设置");
    header->addWidget(globalSettingsButton);
    layout->addLayout(header);

    pages_ = new QStackedWidget;
    pages_->setObjectName("contentPages");

    // Wallpaper library page.
    auto* wallpaperPage = new QWidget;
    auto* wallpaperLayout = new QVBoxLayout(wallpaperPage);
    wallpaperLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    auto* libraryPane = new QFrame;
    libraryPane->setObjectName("libraryPane");
    auto* libraryLayout = new QVBoxLayout(libraryPane);
    libraryLayout->setContentsMargins(18, 18, 18, 18);
    libraryLayout->setSpacing(14);
    auto* libraryHeader = new QHBoxLayout;
    auto* libraryTitle = new QLabel("资料库");
    libraryTitle->setObjectName("sectionTitle");
    libraryCount_ = new QLabel;
    libraryCount_->setObjectName("countLabel");
    auto* addButton = new QPushButton("＋ 添加壁纸");
    addButton->setObjectName("primaryButton");
    auto* settingsButton = new QToolButton;
    settingsButton->setObjectName("settingsButton");
    settingsButton->setText("⚙");
    settingsButton->setToolTip("壁纸设置");
    libraryHeader->addWidget(libraryTitle);
    libraryHeader->addStretch();
    libraryHeader->addWidget(libraryCount_);
    libraryHeader->addSpacing(10);
    libraryHeader->addWidget(addButton);
    libraryHeader->addWidget(settingsButton);
    libraryLayout->addLayout(libraryHeader);
    search_ = new QLineEdit;
    search_->setObjectName("searchBox");
    search_->setPlaceholderText("搜索本地壁纸");
    search_->setClearButtonEnabled(true);
    search_->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                       QLineEdit::LeadingPosition);
    libraryLayout->addWidget(search_);
    list_ = new QListWidget;
    list_->setObjectName("wallpaperGrid");
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWrapping(true);
    list_->setUniformItemSizes(true);
    list_->setSpacing(10);
    list_->setGridSize({172, 164});
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    libraryLayout->addWidget(list_, 1);
    splitter->addWidget(libraryPane);

    auto* detailPane = new QFrame;
    detailPane->setObjectName("detailPane");
    detailPane->setMinimumWidth(350);
    detailPane->setMaximumWidth(430);
    auto* detailPaneLayout = new QVBoxLayout(detailPane);
    detailPaneLayout->setContentsMargins(0, 0, 0, 0);
    auto* detailScroll = new QScrollArea;
    detailScroll->setObjectName("detailScroll");
    detailScroll->setWidgetResizable(true);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailScroll->setFrameShape(QFrame::NoFrame);
    auto* detailContent = new QWidget;
    detailContent->setObjectName("detailContent");
    auto* detailLayout = new QVBoxLayout(detailContent);
    detailLayout->setContentsMargins(20, 20, 20, 20);
    detailLayout->setSpacing(12);
    auto* previewLabel = new QLabel("当前预览");
    previewLabel->setObjectName("eyebrow");
    detailLayout->addWidget(previewLabel);
    preview_ = new PreviewWidget;
    detailLayout->addWidget(preview_);
    title_ = new QLabel("选择一张壁纸");
    title_->setObjectName("wallpaperTitle");
    title_->setWordWrap(true);
    detailLayout->addWidget(title_);
    type_ = new QLabel("未选择");
    type_->setObjectName("typeBadge");
    detailLayout->addWidget(type_, 0, Qt::AlignLeft);
    details_ = new QLabel("从左侧资料库选择壁纸，即可查看动态预览与详细信息。");
    details_->setObjectName("details");
    details_->setWordWrap(true);
    detailLayout->addWidget(details_);
    path_ = new QLabel;
    path_->setObjectName("pathLabel");
    path_->setWordWrap(true);
    detailLayout->addWidget(path_);
    auto* applyButton = new QPushButton("应用到桌面");
    applyButton->setObjectName("primaryButton");
    detailLayout->addWidget(applyButton);
    auto* removeButton = new QPushButton("从资料库移除");
    removeButton->setObjectName("dangerButton");
    detailLayout->addWidget(removeButton);
    auto* restoreButton = new QPushButton("停止并恢复系统壁纸");
    restoreButton->setObjectName("secondaryButton");
    detailLayout->addWidget(restoreButton);
    auto* locationButton = new QToolButton;
    locationButton->setObjectName("locationButton");
    locationButton->setText("打开文件所在位置");
    detailLayout->addWidget(locationButton, 0, Qt::AlignLeft);
    detailLayout->addStretch();
    detailScroll->setWidget(detailContent);
    detailPaneLayout->addWidget(detailScroll);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({830, 390});
    wallpaperLayout->addWidget(splitter);
    pages_->addWidget(wallpaperPage);

    // Dock management page. It deliberately manages the Electron Dock without recreating its visual
    // layer.
    auto* dockPage = new QWidget;
    auto* dockPageLayout = new QHBoxLayout(dockPage);
    dockPageLayout->setContentsMargins(0, 0, 0, 0);
    dockPageLayout->setSpacing(16);
    auto* dockAppsPane = new QFrame;
    dockAppsPane->setObjectName("libraryPane");
    auto* dockAppsLayout = new QVBoxLayout(dockAppsPane);
    dockAppsLayout->setContentsMargins(18, 18, 18, 18);
    dockAppsLayout->setSpacing(14);
    auto* dockAppsHeader = new QHBoxLayout;
    auto* dockAppsTitle = new QLabel("图标栏项目");
    dockAppsTitle->setObjectName("sectionTitle");
    dockAppCount_ = new QLabel;
    dockAppCount_->setObjectName("countLabel");
    dockAppsHeader->addWidget(dockAppsTitle);
    dockAppsHeader->addStretch();
    dockAppsHeader->addWidget(dockAppCount_);
    dockAppsLayout->addLayout(dockAppsHeader);
    auto* dockHint = new QLabel("添加应用、调整顺序和外观设置都在这里完成。");
    dockHint->setObjectName("captionLabel");
    dockHint->setWordWrap(true);
    dockAppsLayout->addWidget(dockHint);
    dockApps_ = new QListWidget;
    dockApps_->setObjectName("dockAppList");
    dockApps_->setSelectionMode(QAbstractItemView::SingleSelection);
    dockApps_->setAlternatingRowColors(false);
    dockApps_->setIconSize(QSize(36, 36));
    dockAppsLayout->addWidget(dockApps_, 1);
    auto* dockAppActions = new QHBoxLayout;
    dockAddButton_ = new QPushButton("＋ 添加应用");
    dockAddButton_->setObjectName("primaryButton");
    dockRemoveButton_ = new QPushButton("移除");
    dockRemoveButton_->setObjectName("secondaryButton");
    dockUpButton_ = new QPushButton("上移");
    dockUpButton_->setObjectName("secondaryButton");
    dockDownButton_ = new QPushButton("下移");
    dockDownButton_->setObjectName("secondaryButton");
    dockAppActions->addWidget(dockAddButton_);
    dockAppActions->addWidget(dockRemoveButton_);
    dockAppActions->addStretch();
    dockAppActions->addWidget(dockUpButton_);
    dockAppActions->addWidget(dockDownButton_);
    dockAppsLayout->addLayout(dockAppActions);
    dockPageLayout->addWidget(dockAppsPane, 3);

    auto* dockSettingsPane = new QFrame;
    dockSettingsPane->setObjectName("detailPane");
    dockSettingsPane->setMinimumWidth(380);
    dockSettingsPane->setMaximumWidth(470);
    auto* dockSettingsLayout = new QVBoxLayout(dockSettingsPane);
    dockSettingsLayout->setContentsMargins(22, 22, 22, 22);
    dockSettingsLayout->setSpacing(13);
    auto* dockTitle = new QLabel("图标栏控制");
    dockTitle->setObjectName("sectionTitle");
    dockSettingsLayout->addWidget(dockTitle);
    dockStatus_ = new QLabel("正在连接图标栏");
    dockStatus_->setObjectName("statusBadge");
    dockSettingsLayout->addWidget(dockStatus_, 0, Qt::AlignLeft);
    dockVisibilityButton_ = new QPushButton("显示 / 隐藏图标栏");
    dockVisibilityButton_->setObjectName("secondaryButton");
    dockSettingsLayout->addWidget(dockVisibilityButton_);
    auto* resetDockButton = new QPushButton("恢复默认位置");
    resetDockButton->setObjectName("secondaryButton");
    dockSettingsLayout->addWidget(resetDockButton);
    auto* orientationLabel = new QLabel("排列方向");
    orientationLabel->setObjectName("fieldLabel");
    dockSettingsLayout->addWidget(orientationLabel);
    dockOrientation_ = new QComboBox;
    dockOrientation_->addItem("横向", "horizontal");
    dockOrientation_->addItem("纵向", "vertical");
    dockSettingsLayout->addWidget(dockOrientation_);
    auto* scaleRow = new QHBoxLayout;
    auto* scaleLabel = new QLabel("整体大小");
    scaleLabel->setObjectName("fieldLabel");
    dockScaleValue_ = new QLabel("100%");
    dockScaleValue_->setObjectName("countLabel");
    scaleRow->addWidget(scaleLabel);
    scaleRow->addStretch();
    scaleRow->addWidget(dockScaleValue_);
    dockSettingsLayout->addLayout(scaleRow);
    dockScale_ = new QSlider(Qt::Horizontal);
    dockScale_->setRange(65, 150);
    dockScale_->setSingleStep(5);
    dockSettingsLayout->addWidget(dockScale_);
    dockScaleTimer_ = new QTimer(this);
    dockScaleTimer_->setSingleShot(true);
    dockScaleTimer_->setInterval(70);
    dockEnabled_ = new QCheckBox("启用桌面图标栏");
    dockHideButton_ = new QCheckBox("启用收起按钮");
    dockDesktopIconsButton_ = new QCheckBox("启用清空桌面按钮");
    dockDesktopIconsHidden_ = new QCheckBox("隐藏桌面图标");
    dockAlwaysOnTop_ = new QCheckBox("始终将图标栏置于顶层");
    dockSettingsLayout->addWidget(dockEnabled_);
    dockSettingsLayout->addWidget(dockHideButton_);
    dockSettingsLayout->addWidget(dockDesktopIconsButton_);
    dockSettingsLayout->addWidget(dockDesktopIconsHidden_);
    dockSettingsLayout->addWidget(dockAlwaysOnTop_);
    taskbarTransparency_ = new QCheckBox("启用透明任务栏");
    taskbarTransparency_->setChecked(translucentTb_ && translucentTb_->enabled());
    dockSettingsLayout->addWidget(taskbarTransparency_);
    dockSettingsLayout->addStretch();
    dockPageLayout->addWidget(dockSettingsPane, 2);
    pages_->addWidget(dockPage);
    layout->addWidget(pages_, 1);
    setCentralWidget(root);

    setStyleSheet(R"(
        QWidget#root, QStackedWidget#contentPages { background: #ffffff; color: #1d2b3d; font-family: "Microsoft YaHei UI"; }
        QLabel#brandName { color: #17263a; font-size: 20px; font-weight: 700; letter-spacing: 1.5px; }
        QLabel#brandCaption, QLabel#countLabel, QLabel#eyebrow, QLabel#captionLabel { color: #6f8096; font-size: 12px; }
        QLabel#sectionTitle { color: #1d2b3d; font-size: 17px; font-weight: 700; }
        QLabel#fieldLabel { color: #4a6078; font-size: 12px; font-weight: 600; }
        QLabel#statusBadge { color: #317ac8; background: #eef6ff; border: 1px solid #b9d6f5; border-radius: 10px; padding: 4px 9px; font-size: 11px; font-weight: 600; }
        QPushButton#pageTab { min-width: 112px; min-height: 42px; color: #425b76; background: #ffffff; border: 1px solid #cbd9e8; border-radius: 10px; font-size: 17px; font-weight: 700; }
        QPushButton#pageTab:checked { color: #ffffff; background: #3f8fe8; border-color: #3f8fe8; }
        QFrame#libraryPane, QFrame#detailPane { background: #ffffff; border: 1px solid #d9e3ef; border-radius: 14px; }
        QWidget#detailContent, QScrollArea#detailScroll { background: #ffffff; }
        QScrollBar:vertical { width: 10px; background: #ffffff; margin: 0 2px; } QScrollBar::handle:vertical { min-height: 28px; background: #c9d7e8; border-radius: 5px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: transparent; border: none; } QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical { width: 0; height: 0; }
        QSplitter::handle { background: #ffffff; } QLineEdit#searchBox, QComboBox { background: #ffffff; color: #1d2b3d; border: 1px solid #cbd9e8; border-radius: 9px; padding: 9px 10px; font-size: 13px; }
        QLineEdit#searchBox:focus, QComboBox:focus { border-color: #4e98ee; } QComboBox::drop-down { border: none; background: transparent; width: 26px; } QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
        QListWidget#wallpaperGrid, QListWidget#dockAppList { background: #ffffff; border: none; outline: 0; }
        QListWidget#wallpaperGrid::item { background: transparent; border: none; padding: 0; }
        QListWidget#dockAppList::item { border: 1px solid #d9e3ef; border-radius: 8px; margin: 3px 0; padding: 10px; color: #243c57; } QListWidget#dockAppList::item:selected { background: #eef6ff; border-color: #4e98ee; }
        QFrame#wallpaperCard { background: #ffffff; border: 1px solid #d9e3ef; border-radius: 9px; } QFrame#wallpaperCard:hover { background: #f5f9ff; border-color: #8fb8e8; } QFrame#wallpaperCard[selected="true"] { background: #eef6ff; border: 2px solid #4e98ee; }
        QLabel#cardCover { color: #7a8ca0; background: #f3f6fa; border: none; border-radius: 5px; } QLabel#cardTitle { color: #1d2b3d; background: transparent; border: none; font-size: 12px; font-weight: 600; } QLabel#cardType { color: #667c94; background: transparent; border: none; font-size: 11px; }
        QPushButton { min-height: 34px; border-radius: 8px; padding: 0 13px; font-weight: 600; } QPushButton#primaryButton { color: #ffffff; background: #3f8fe8; border: 1px solid #3f8fe8; } QPushButton#primaryButton:hover { background: #287edc; }
        QPushButton#dangerButton { color: #c43b4d; background: #ffffff; border: 1px solid #e3a0aa; } QPushButton#secondaryButton { color: #3a5b7d; background: #ffffff; border: 1px solid #b9cce1; } QPushButton#secondaryButton:hover { background: #f4f8fc; }
        QToolButton#settingsButton { color: #3a5b7d; background: #ffffff; border: 1px solid #b9cce1; border-radius: 9px; padding: 2px 9px 5px; font-family: "Segoe UI Symbol"; font-size: 20px; } QToolButton#locationButton { color: #317ac8; border: none; padding: 4px 0; }
        QLabel#wallpaperTitle { color: #1d2b3d; font-size: 19px; font-weight: 700; } QLabel#typeBadge { color: #317ac8; background: #eef6ff; border: 1px solid #b9d6f5; border-radius: 10px; padding: 4px 9px; font-size: 11px; font-weight: 600; }
        QLabel#details { color: #4a6078; font-size: 13px; line-height: 1.4; } QLabel#pathLabel { color: #7790aa; font-size: 11px; } QCheckBox { color: #314d6b; spacing: 8px; } QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #9db4cf; border-radius: 4px; background: #ffffff; } QCheckBox::indicator:checked { background: #3f8fe8; border-color: #3f8fe8; }
        QSlider::groove:horizontal { height: 5px; background: #d8e4f1; border-radius: 2px; } QSlider::sub-page:horizontal { background: #4e98ee; border-radius: 2px; } QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #3f8fe8; }
    )");

    storePage_ = new StorePage(library_, this);
    pages_->addWidget(storePage_);
    connect(storeTab_, &QPushButton::clicked, this, [this] { setActivePage(2); });
    connect(storePage_, &StorePage::applyLocalEntry, this, [this](const QString& id) {
        rebuildList();
        for (int i = 0; i < list_->count(); ++i) {
            if (list_->item(i)->data(Qt::UserRole).toString() == id) {
                list_->setCurrentRow(i);
                apply();
                return;
            }
        }
        // A search filter must never prevent applying a downloaded wallpaper.
        search_->clear();
        rebuildList();
        for (int i = 0; i < list_->count(); ++i)
            if (list_->item(i)->data(Qt::UserRole).toString() == id) { list_->setCurrentRow(i); apply(); return; }
    });
    connect(wallpaperTab_, &QPushButton::clicked, this, &MainWindow::showWallpaperPage);
    connect(dockTab_, &QPushButton::clicked, this, &MainWindow::showDockPage);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::add);
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::showWallpaperSettings);
    connect(globalSettingsButton, &QToolButton::clicked, this, &MainWindow::showSettings);
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::rebuildList);
    connect(list_, &QListWidget::itemSelectionChanged, this, &MainWindow::selectionChanged);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { apply(); });
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::apply);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::remove);
    connect(restoreButton, &QPushButton::clicked, this, &MainWindow::restoreSystem);
    connect(locationButton, &QToolButton::clicked, this, &MainWindow::openLocation);
    connect(dockAddButton_, &QPushButton::clicked, this, &MainWindow::addDockApplications);
    connect(dockRemoveButton_, &QPushButton::clicked, this, &MainWindow::removeDockApplication);
    connect(dockUpButton_, &QPushButton::clicked, this, &MainWindow::moveDockApplicationUp);
    connect(dockDownButton_, &QPushButton::clicked, this, &MainWindow::moveDockApplicationDown);
    connect(dockVisibilityButton_, &QPushButton::clicked, this, [this] {
        if (icontra_)
            icontra_->toggleDock();
    });
    connect(resetDockButton, &QPushButton::clicked, this, [this] {
        if (icontra_)
            icontra_->resetDockPosition();
    });
    connect(dockEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!updatingDockPanel_ && icontra_)
            icontra_->setEnabled(enabled);
    });
    connect(dockOrientation_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::sendDockSettings);
    connect(dockScale_, &QSlider::valueChanged, this,
            [this](int value) { dockScaleValue_->setText(QString::number(value) + "%"); });
    connect(dockScale_, &QSlider::sliderPressed, this, [this] { dockScaleDirty_ = true; });
    connect(dockScale_, &QSlider::sliderMoved, this, [this] {
        dockScaleDirty_ = true;
        dockScaleTimer_->start();
    });
    connect(dockScale_, &QSlider::sliderReleased, this, [this] {
        dockScaleDirty_ = true;
        flushDockScale();
    });
    connect(dockScaleTimer_, &QTimer::timeout, this, &MainWindow::flushDockScale);
    for (auto* control :
         {dockHideButton_, dockDesktopIconsButton_, dockDesktopIconsHidden_, dockAlwaysOnTop_})
        connect(control, &QCheckBox::toggled, this, &MainWindow::sendDockSettings);
    connect(taskbarTransparency_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!translucentTb_)
            return;
        const auto result = translucentTb_->setEnabled(enabled);
        if (!result.ok)
            showMessage("透明任务栏", result.message);
    });
    connect(engine_, &WallpaperEngine::failed, this,
            [this](const Result& result) { showMessage("壁纸播放失败", result.message); });
    if (icontra_) {
        connect(icontra_, &IcontraHost::dockStateChanged, this, &MainWindow::updateDockPanel);
        connect(icontra_, &IcontraHost::statusChanged, this, [this](const QString& status) {
            dockStatus_->setText(status);
            updateDockControlsEnabled();
        });
        connect(icontra_, &IcontraHost::readyChanged, this,
                [this](bool) { updateDockControlsEnabled(); });
    }

    rebuildList();
    setupTray();
    setActivePage(2);
    updateDockPanel(icontra_ ? icontra_->dockState() : QJsonObject{});
    updateDockControlsEnabled();
    const auto geometry = AppSettings().windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void MainWindow::setupTray() {
    tray_ = new QSystemTrayIcon(QApplication::windowIcon(), this);
    auto* menu = new QMenu(this);
    trayAutoStartAction_ = menu->addAction("开机自动启动");
    trayAutoStartAction_->setCheckable(true);
    trayAutoStartAction_->setChecked(AutoStartManager::isEnabled());
    menu->addAction("打开 ZloWallpaper", this, &MainWindow::raiseFromInstance);
    menu->addAction("壁纸商城", this, [this] { raiseFromInstance(); setActivePage(2); });
    menu->addAction("壁纸库管理", this, &MainWindow::showWallpaperPage);
    menu->addAction("图标栏管理", this, &MainWindow::showDockPage);
    menu->addSeparator();
    menu->addAction("暂停壁纸", this, [this] { engine_->pause(true); });
    menu->addAction("恢复壁纸", this, [this] { engine_->pause(false); });
    menu->addAction("停止壁纸", this, &MainWindow::stop);
    menu->addSeparator();

    auto* toggleDock = menu->addAction("显示 / 隐藏图标栏", this, [this] {
        if (icontra_)
            icontra_->toggleDock();
    });
    auto* enableDock = menu->addAction("启用自定义图标栏");
    enableDock->setCheckable(true);
    enableDock->setChecked(icontra_ && icontra_->enabled());
    trayDockHideButtonAction_ = menu->addAction("启用收起按钮");
    trayDockHideButtonAction_->setCheckable(true);
    trayDockDesktopButtonAction_ = menu->addAction("启用清空桌面按钮");
    trayDockDesktopButtonAction_->setCheckable(true);
    connect(enableDock, &QAction::toggled, this, [this](bool enabled) {
        if (icontra_)
            icontra_->setEnabled(enabled);
    });
    connect(trayAutoStartAction_, &QAction::toggled, this, [this](bool enabled) {
        const auto result = AutoStartManager::setEnabled(enabled);
        if (result.ok)
            return;
        const QSignalBlocker blocker(trayAutoStartAction_);
        trayAutoStartAction_->setChecked(AutoStartManager::isEnabled());
        showMessage("开机自动启动", result.message);
    });
    connect(trayDockHideButtonAction_, &QAction::toggled, this, [this](bool enabled) {
        if (icontra_ && icontra_->ready())
            icontra_->updateDockSettings(QJsonObject{{"hideButtonEnabled", enabled}});
    });
    connect(trayDockDesktopButtonAction_, &QAction::toggled, this, [this](bool enabled) {
        if (icontra_ && icontra_->ready())
            icontra_->updateDockSettings(QJsonObject{{"desktopIconButtonEnabled", enabled}});
    });
    if (icontra_) {
        connect(icontra_, &IcontraHost::readyChanged, toggleDock, &QAction::setEnabled);
        connect(icontra_, &IcontraHost::readyChanged, trayDockHideButtonAction_,
                &QAction::setEnabled);
        connect(icontra_, &IcontraHost::readyChanged, trayDockDesktopButtonAction_,
                &QAction::setEnabled);
        const bool ready = icontra_->ready();
        toggleDock->setEnabled(ready);
        trayDockHideButtonAction_->setEnabled(ready);
        trayDockDesktopButtonAction_->setEnabled(ready);
    }
    menu->addSeparator();
    menu->addAction("退出程序", qApp, &QApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    raiseFromInstance();
            });
    tray_->show();
}

void MainWindow::rebuildList() {
    const auto selectedId = selected() ? selected()->id : QString{};
    const auto query = search_->text().trimmed();
    list_->clear();
    int visible = 0;
    for (const auto& entry : library_->entries()) {
        if (!query.isEmpty() && !entry.fileName.contains(query, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, entry.id);
        item->setSizeHint({166, 158});
        list_->setItemWidget(item, new WallpaperCard(entry, list_));
        if (entry.id == selectedId)
            list_->setCurrentItem(item);
        ++visible;
    }
    libraryCount_->setText(QString("%1 张壁纸").arg(visible));
    if (list_->currentItem())
        selectionChanged();
    else
        updateDetails(nullptr);
}
int MainWindow::displayedEntryCount() const {
    return list_->count();
}
const WallpaperEntry* MainWindow::selected() const {
    const auto* item = list_->currentItem();
    return item ? library_->find(item->data(Qt::UserRole).toString()) : nullptr;
}
void MainWindow::updateDetails(const WallpaperEntry* entry) {
    preview_->preview(entry);
    if (!entry) {
        title_->setText("选择一张壁纸");
        type_->setText("未选择");
        details_->setText("从左侧资料库选择壁纸，即可查看动态预览与详细信息。");
        path_->clear();
        return;
    }
    const QFileInfo file(entry->path);
    title_->setText(entry->fileName);
    type_->setText(WallpaperEntry::typeName(entry->type));
    details_->setText(QString("%1 · %2\n状态：%3")
                          .arg(entry->mimeType.isEmpty() ? WallpaperEntry::typeName(entry->type)
                                                         : entry->mimeType)
                          .arg(QString::number(file.size() / 1024 / 1024) + " MB")
                          .arg(entry->valid ? "可用" : "原文件缺失"));
    path_->setText(entry->path);
}
void MainWindow::selectionChanged() {
    for (int row = 0; row < list_->count(); ++row)
        if (auto* card = dynamic_cast<WallpaperCard*>(list_->itemWidget(list_->item(row))))
            card->setSelected(list_->item(row) == list_->currentItem());
    updateDetails(selected());
}
void MainWindow::add() {
    const auto paths = QFileDialog::getOpenFileNames(this, "添加本地壁纸", {}, mediaFilter());
    QString last;
    for (const auto& path : paths) {
        const auto result = library_->add(path);
        if (!result.ok)
            showMessage("添加失败", result.message);
        else
            last = QFileInfo(path).absoluteFilePath();
    }
    if (!last.isEmpty())
        rebuildList();
}
void MainWindow::remove() {
    const auto* entry = selected();
    if (!entry)
        return;
    if (QMessageBox::question(this, "移除壁纸", "仅从资料库移除，不会删除原文件。是否继续？") !=
        QMessageBox::Yes)
        return;
    if (engine_->active() && engine_->activeId() == entry->id)
        stop();
    library_->remove(entry->id);
}
void MainWindow::apply() {
    const auto* selectedEntry = selected();
    if (!selectedEntry)
        return;
    auto* entry = library_->find(selectedEntry->id);
    if (!entry->valid)
        return;
    auto copy = *entry;
    copy.displayMode = DisplayMode::Fill;
    copy.muted = AppSettings().muteVideos();
    const auto result = engine_->apply(copy);
    if (!result.ok) {
        showMessage("设置壁纸失败", result.message);
        return;
    }
    if (auto* stored = library_->find(copy.id)) {
        stored->displayMode = copy.displayMode;
        stored->muted = copy.muted;
        stored->lastUsedAt = QDateTime::currentDateTime();
        library_->save();
    }
    AppSettings().setActiveWallpaperId(copy.id);
    tray_->showMessage("ZloWallpaper", "壁纸已应用到桌面。", QSystemTrayIcon::Information, 2500);
}
void MainWindow::stop() {
    engine_->stop();
    AppSettings().setActiveWallpaperId({});
}
void MainWindow::restoreSystem() {
    stop();
    const auto path = AppSettings().originalSystemWallpaper();
    const auto wide = path.toStdWString();
    if (!wide.empty() &&
        !SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(wide.c_str()),
                               SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        showMessage("恢复失败", QString("系统壁纸恢复失败，Win32 错误：%1").arg(GetLastError()));
}
void MainWindow::openLocation() {
    if (const auto* entry = selected())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(entry->path).absolutePath()));
}
void MainWindow::showSettings() {
    SettingsDialog(SettingsDialog::Page::Global, this).exec();
    if (trayAutoStartAction_) {
        const QSignalBlocker blocker(trayAutoStartAction_);
        trayAutoStartAction_->setChecked(AutoStartManager::isEnabled());
    }
}
void MainWindow::showWallpaperSettings() {
    SettingsDialog(SettingsDialog::Page::Wallpaper, this).exec();
}
void MainWindow::setActivePage(int index) {
    pages_->setCurrentIndex(index);
    wallpaperTab_->setChecked(index == 0);
    dockTab_->setChecked(index == 1);
    storeTab_->setChecked(index == 2);
    if (index != 0)
        preview_->setPlaybackActive(false);
    else if (isVisible() && !isMinimized())
        preview_->setPlaybackActive(true);
}
void MainWindow::showWallpaperPage() {
    setActivePage(0);
}
void MainWindow::showDockPage() {
    raiseFromInstance();
    setActivePage(1);
}
void MainWindow::updateDockPanel(const QJsonObject& state) {
    updatingDockPanel_ = true;
    const QSignalBlocker enabledBlock(dockEnabled_);
    const QSignalBlocker orientationBlock(dockOrientation_);
    const QSignalBlocker scaleBlock(dockScale_);
    const QSignalBlocker hideBlock(dockHideButton_);
    const QSignalBlocker desktopButtonBlock(dockDesktopIconsButton_);
    const QSignalBlocker desktopHiddenBlock(dockDesktopIconsHidden_);
    const QSignalBlocker alwaysOnTopBlock(dockAlwaysOnTop_);
    dockEnabled_->setChecked(icontra_ && icontra_->enabled());

    if (!state.isEmpty()) {
        dockOrientation_->setCurrentIndex(state.value("orientation").toString() == "vertical" ? 1
                                                                                              : 0);
        const int scale = qRound(state.value("scale").toDouble(1) * 100);
        // A stale asynchronous reply must never overwrite an active slider
        // gesture. The next matching acknowledgement clears the pending state.
        if (!dockScaleDirty_ || scale == dockScale_->value()) {
            dockScale_->setValue(scale);
            dockScaleValue_->setText(QString::number(scale) + "%");
            dockScaleDirty_ = false;
        }
        dockHideButton_->setChecked(state.value("hideButtonEnabled").toBool());
        dockDesktopIconsButton_->setChecked(state.value("desktopIconButtonEnabled").toBool());
        dockDesktopIconsHidden_->setChecked(state.value("desktopIconsHidden").toBool());
        dockAlwaysOnTop_->setChecked(state.value("alwaysOnTop").toBool(true));
        if (trayDockHideButtonAction_) {
            const QSignalBlocker blocker(trayDockHideButtonAction_);
            trayDockHideButtonAction_->setChecked(state.value("hideButtonEnabled").toBool());
        }
        if (trayDockDesktopButtonAction_) {
            const QSignalBlocker blocker(trayDockDesktopButtonAction_);
            trayDockDesktopButtonAction_->setChecked(
                state.value("desktopIconButtonEnabled").toBool());
        }

        if (state.value("appsChanged").toBool() || state.contains("apps")) {
            const auto applications = state.value("apps").toArray();
            QStringList signatureParts;
            for (const auto& value : applications) {
                const auto app = value.toObject();
                signatureParts << app.value("id").toString() + "\x1f" +
                                      app.value("name").toString() + "\x1f" +
                                      app.value("path").toString();
            }
            const auto fingerprint = signatureParts.join("\x1e");
            if (fingerprint != dockAppsFingerprint_) {
                const auto selectedId =
                    dockApps_->currentItem()
                        ? dockApps_->currentItem()->data(Qt::UserRole).toString()
                        : QString{};
                const int scrollValue = dockApps_->verticalScrollBar()->value();
                dockApps_->clear();
                for (const auto& value : applications) {
                    const auto app = value.toObject();
                    const auto id = app.value("id").toString();
                    auto* item = new QListWidgetItem(app.value("name").toString() + "\n" +
                                                         app.value("path").toString(),
                                                     dockApps_);
                    item->setData(Qt::UserRole, id);
                    item->setSizeHint(QSize(0, 66));
                    item->setToolTip(app.value("path").toString());
                    if (dockIconCache_.contains(id))
                        item->setIcon(dockIconCache_.value(id));
                    if (id == selectedId)
                        dockApps_->setCurrentItem(item);
                }
                dockAppsFingerprint_ = fingerprint;
                dockApps_->verticalScrollBar()->setValue(
                    qMin(scrollValue, dockApps_->verticalScrollBar()->maximum()));
                decodeDockIconsAsync(applications);
            }
            dockAppCount_->setText(QString("%1 个应用").arg(applications.size()));
        }
    }
    updatingDockPanel_ = false;
    updateDockControlsEnabled();
}
void MainWindow::updateDockControlsEnabled() {
    const bool ready = icontra_ && icontra_->ready();
    const bool enabled = icontra_ && icontra_->enabled();
    const QList<QWidget*> controls{
        dockVisibilityButton_,   dockAddButton_,          dockRemoveButton_, dockUpButton_,
        dockDownButton_,         dockOrientation_,        dockScale_,        dockHideButton_,
        dockDesktopIconsButton_, dockDesktopIconsHidden_, dockAlwaysOnTop_};
    for (auto* widget : controls)
        widget->setEnabled(ready && enabled);
    if (!enabled)
        dockStatus_->setText("图标栏已禁用");
}
void MainWindow::decodeDockIconsAsync(const QJsonArray& applications) {
    QHash<QString, QString> encodedIcons;
    for (const auto& value : applications) {
        const auto app = value.toObject();
        const auto id = app.value("id").toString();
        const auto icon = app.value("icon").toString();
        if (!id.isEmpty() && !icon.isEmpty() && !dockIconCache_.contains(id))
            encodedIcons.insert(id, icon);
    }
    if (encodedIcons.isEmpty())
        return;

    const auto generation = ++dockIconDecodeGeneration_;
    auto* watcher = new QFutureWatcher<QHash<QString, QImage>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, QImage>>::finished, this,
            [this, watcher, generation] {
                const auto decoded = watcher->result();
                watcher->deleteLater();
                if (generation != dockIconDecodeGeneration_)
                    return;
                for (auto it = decoded.cbegin(); it != decoded.cend(); ++it) {
                    if (it.value().isNull())
                        continue;
                    dockIconCache_.insert(it.key(), QIcon(QPixmap::fromImage(it.value())));
                    for (int row = 0; row < dockApps_->count(); ++row) {
                        auto* item = dockApps_->item(row);
                        if (item->data(Qt::UserRole).toString() == it.key())
                            item->setIcon(dockIconCache_.value(it.key()));
                    }
                }
            });
    watcher->setFuture(QtConcurrent::run([encodedIcons] {
        QHash<QString, QImage> decoded;
        for (auto it = encodedIcons.cbegin(); it != encodedIcons.cend(); ++it)
            decoded.insert(it.key(), imageFromDataUrl(it.value()));
        return decoded;
    }));
}

void MainWindow::addDockApplications() {
    if (!icontra_)
        return;
    const auto paths =
        QFileDialog::getOpenFileNames(this, "添加图标栏应用", {}, dockApplicationFilter());
    if (!paths.isEmpty())
        icontra_->addDockApplications(paths);
}
void MainWindow::removeDockApplication() {
    if (!icontra_ || !dockApps_->currentItem())
        return;
    icontra_->removeDockApplication(dockApps_->currentItem()->data(Qt::UserRole).toString());
}
void MainWindow::moveDockApplicationUp() {
    if (!icontra_ || !dockApps_->currentItem())
        return;
    const int row = dockApps_->currentRow();
    if (row > 0)
        icontra_->moveDockApplication(dockApps_->currentItem()->data(Qt::UserRole).toString(),
                                      row - 1);
}
void MainWindow::moveDockApplicationDown() {
    if (!icontra_ || !dockApps_->currentItem())
        return;
    const int row = dockApps_->currentRow();
    if (row >= 0 && row + 1 < dockApps_->count())
        icontra_->moveDockApplication(dockApps_->currentItem()->data(Qt::UserRole).toString(),
                                      row + 1);
}
void MainWindow::sendDockSettings() {
    if (updatingDockPanel_ || !icontra_ || !icontra_->ready())
        return;
    icontra_->updateDockSettings(
        QJsonObject{{"orientation", dockOrientation_->currentData().toString()},
                    {"scale", dockScale_->value() / 100.0},
                    {"hideButtonEnabled", dockHideButton_->isChecked()},
                    {"desktopIconButtonEnabled", dockDesktopIconsButton_->isChecked()},
                    {"desktopIconsHidden", dockDesktopIconsHidden_->isChecked()},
                    {"taskbarTransparent", false},
                    {"alwaysOnTop", dockAlwaysOnTop_->isChecked()}});
}
void MainWindow::flushDockScale() {
    if (dockScaleTimer_)
        dockScaleTimer_->stop();
    sendDockSettings();
}
void MainWindow::showMessage(const QString& title, const QString& text) {
    QMessageBox::warning(this, title, text);
}
void MainWindow::raiseFromInstance() {
    showNormal();
    raise();
    activateWindow();
}
void MainWindow::closeEvent(QCloseEvent* event) {
    AppSettings().setWindowGeometry(saveGeometry());
    if (AppSettings().minimizeToTray() && tray_ && tray_->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    event->accept();
}
void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange)
        preview_->setPlaybackActive(!isMinimized() && pages_->currentIndex() == 0);
    QMainWindow::changeEvent(event);
}
void MainWindow::hideEvent(QHideEvent* event) {
    preview_->setPlaybackActive(false);
    QMainWindow::hideEvent(event);
}
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    preview_->setPlaybackActive(pages_->currentIndex() == 0);
}
