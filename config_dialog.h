#pragma once

#include <QDialog>

class QSpinBox;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class ConfigFile;


class ConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfigDialog(ConfigFile& config, QWidget* parent);

private slots:
    void save_settings();
    void cancel_settings();

private:
    ConfigFile& config_;

    // Widgets
    QSpinBox* spin_dpi_;
    QSpinBox* spin_border_margin_;
    QSpinBox* spin_max_recent_documents_;
    QSpinBox* spin_save_cadence_;

    QComboBox* combo_theme_;
    QComboBox* combo_log_level_;

    QComboBox* combo_horiz_alignment_;
    QComboBox* combo_vert_alignment_;
    QCheckBox* check_restore_window_position_;
    QCheckBox* check_restore_documents_;
    QCheckBox* check_allow_oversize_;
    QCheckBox* check_show_menu_;
    QCheckBox* check_show_toolbar_;
    QCheckBox* check_show_statusbar_;
    QCheckBox* check_horiz_tabs_;
    QCheckBox* check_allow_delete_;
    QCheckBox* check_append_log_;

    QPushButton* btn_save_;
    QPushButton* btn_cancel_;

    void setup_ui();
    void load_settings();
    void setup_connections();
};
