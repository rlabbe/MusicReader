#pragma once

#include <QDialog>
#include "config_file.h"

class QSpinBox;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;

class ConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigDialog(ConfigFile &config, QWidget *parent = nullptr);

private slots:
    void browse_music_directory();
    void save_settings();
    void cancel_settings();

private:
    ConfigFile &config_;

    // Widgets
    QSpinBox *spin_max_recent_documents_;
    QSpinBox *spin_page_view_count_;

    QComboBox *combo_theme_;
    QComboBox *combo_log_level_;

    QCheckBox *check_restore_window_position_;
    QCheckBox *check_restore_documents_;
    QCheckBox *check_zoom_to_content_;
    QCheckBox *check_allow_oversize_;

    QLineEdit *edit_music_directory_;
    QPushButton *btn_browse_;

    QPushButton *btn_save_;
    QPushButton *btn_cancel_;

    void setup_ui();
    void load_settings();
    void setup_connections();
};
