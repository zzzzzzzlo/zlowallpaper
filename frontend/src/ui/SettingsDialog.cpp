#include "ui/SettingsDialog.h"
#include "config/AppSettings.h"
#include "system/AutoStartManager.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

SettingsDialog::SettingsDialog(Page page, QWidget* parent) : QDialog(parent), page_(page) {
    const bool globalPage = page_ == Page::Global;
    setWindowTitle(globalPage ? "ZloWallpaper 全局设置" : "ZloWallpaper 壁纸设置");
    setMinimumWidth(420);
    setModal(true);

    AppSettings settings;
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(13);

    auto* heading = new QLabel(globalPage ? "全局设置" : "壁纸设置");
    heading->setObjectName("settingsHeading");
    auto* caption = new QLabel(globalPage ? "管理程序启动、窗口行为与诊断记录。"
                                          : "这些选项会影响壁纸恢复和动态播放行为。");
    caption->setObjectName("settingsCaption");
    caption->setWordWrap(true);
    layout->addRow(heading);
    layout->addRow(caption);

    if (globalPage) {
        autostart_ = new QCheckBox("开机自动启动");
        autostart_->setChecked(AutoStartManager::isEnabled());
        tray_ = new QCheckBox("关闭窗口时最小化到系统托盘");
        tray_->setChecked(settings.minimizeToTray());
        log_ = new QCheckBox("记录诊断日志");
        log_->setChecked(settings.loggingEnabled());
        layout->addRow("启动", autostart_);
        layout->addRow("窗口", tray_);
        layout->addRow("诊断", log_);
    } else {
        restore_ = new QCheckBox("启动后恢复上次使用的壁纸");
        restore_->setChecked(settings.restoreLastWallpaper());
        mute_ = new QCheckBox("动态预览与壁纸默认静音");
        mute_->setChecked(settings.muteVideos());
        autoPause_ = new QCheckBox("全屏程序覆盖显示器时暂停壁纸");
        autoPause_->setChecked(settings.pauseWhenInactive());
        layout->addRow("恢复", restore_);
        layout->addRow("声音", mute_);
        layout->addRow("播放", autoPause_);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存设置");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setStyleSheet(R"(
        QDialog { background: #ffffff; color: #1d2b3d; font-family: "Microsoft YaHei UI"; }
        QLabel#settingsHeading { color: #1d2b3d; font-size: 19px; font-weight: 700; }
        QLabel#settingsCaption { color: #71859b; font-size: 12px; padding-bottom: 8px; }
        QLabel { color: #4a6078; }
        QCheckBox { color: #1d2b3d; spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #9db4cf; border-radius: 4px; background: #ffffff; }
        QCheckBox::indicator:checked { background: #3f8fe8; border-color: #3f8fe8; image: url(:/icons/checkmark.svg); }
        QDialogButtonBox QPushButton { min-height: 32px; border-radius: 7px; padding: 0 14px; color: #3a5b7d; background: #ffffff; border: 1px solid #b9cce1; }
        QDialogButtonBox QPushButton[text="保存设置"] { color: #ffffff; background: #3f8fe8; border-color: #3f8fe8; }
    )");
}

void SettingsDialog::save() {
    AppSettings settings;
    if (page_ == Page::Global) {
        const auto result = AutoStartManager::setEnabled(autostart_->isChecked());
        if (!result.ok) {
            QMessageBox::warning(this, "启动项设置失败", result.message);
            return;
        }
        settings.setMinimizeToTray(tray_->isChecked());
        settings.setLoggingEnabled(log_->isChecked());
    } else {
        settings.setRestoreLastWallpaper(restore_->isChecked());
        settings.setMuteVideos(mute_->isChecked());
        settings.setPauseWhenInactive(autoPause_->isChecked());
    }
    accept();
}
