#pragma once
#include <QDialog>
class QCheckBox;
class QComboBox;
class SettingsDialog final : public QDialog {
    Q_OBJECT
  public:
    enum class Page { Global, Wallpaper };
    explicit SettingsDialog(Page page, QWidget* parent = nullptr);
  private slots:
    void save();

  private:
    Page page_;
    QCheckBox* autostart_ = nullptr;
    QCheckBox* restore_ = nullptr;
    QCheckBox* tray_ = nullptr;
    QCheckBox* mute_ = nullptr;
    QCheckBox* autoPause_ = nullptr;
    QCheckBox* log_ = nullptr;
};
