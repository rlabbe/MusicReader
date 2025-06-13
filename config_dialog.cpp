#include "config_dialog.h"
#include <QSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include "config_file.h"


ConfigDialog::ConfigDialog(ConfigFile &config, QWidget *parent)
    : QDialog(parent), config_(config)
{
    setWindowTitle("Settings");
    setWindowFlags(windowFlags() | Qt::WindowContextHelpButtonHint);
    setup_ui();
    load_settings();
    setup_connections();
}


void ConfigDialog::setup_ui()
{
    spin_dpi_ = new QSpinBox(this);
    spin_dpi_->setRange(72, 360);
    spin_dpi_->setWhatsThis("PDF rendering resolution in dots per inch. Higher values provide sharper images but use more memory. 360 for 4K display is good");

    spin_border_margin_ = new QSpinBox(this);
    spin_border_margin_->setRange(0, 100);
    spin_border_margin_->setWhatsThis("Padding space in pixels around the document when zoomed in (hiding the white border)");

    spin_max_recent_documents_ = new QSpinBox(this);
    spin_max_recent_documents_->setRange(1, 100);
    spin_max_recent_documents_->setWhatsThis("Maximum number of recently opened documents to remember in the File menu.");

    spin_save_cadence_ = new QSpinBox(this);
    spin_save_cadence_->setRange(0, 5 * 60);
    spin_save_cadence_->setWhatsThis("Automatically save the document every specified number of seconds. Set to 0 to disable automatic saving.");

    /*
    combo_theme_ = new QComboBox(this);
    combo_theme_->addItem("Dark", static_cast<int>(Theme::Dark));
    combo_theme_->addItem("Light", static_cast<int>(Theme::Light));
    */

    combo_log_level_ = new QComboBox(this);
    combo_log_level_->addItem("Normal", static_cast<int>(LogLevel::Normal));
    combo_log_level_->addItem("Diagnostic", static_cast<int>(LogLevel::Diagnostic));
    combo_log_level_->setWhatsThis("Logging verbosity level. Normal for standard operation, Diagnostic for troubleshooting and detailed debugging information.");

    check_center_ = new QCheckBox("Center Document in window", this);
    check_center_->setWhatsThis("Center the PDF document in the view area. When unchecked, documents are aligned to the left side.");

    check_restore_window_position_ = new QCheckBox("Restore Window Position On Startup", this);
    check_restore_window_position_->setWhatsThis("Remember and restore the application window size and position when the program starts.");

    check_restore_documents_ = new QCheckBox("Restore Documents On Startup", this);
    check_restore_documents_->setWhatsThis("Automatically reopen the documents that were open when the application was last closed.");

    check_allow_oversize_ = new QCheckBox("Allow > 100% page zoom (recommended)", this);
    check_allow_oversize_->setWhatsThis("Allow zooming beyond 100% to magnify documents larger than their natural size. Recommended for detailed viewing of sheet music.");

    check_show_menu_ = new QCheckBox("Menu Bar (click app title bar icon for menu if not shown)", this);
    check_show_menu_->setWhatsThis("Show the menu bar with File, Edit, View, and other menus. If hidden, you can access the menu by clicking the application icon in the title bar.");

    check_show_toolbar_ = new QCheckBox("Tool Bar", this);
    check_show_toolbar_->setWhatsThis("Show the toolbar with buttons for common actions like Open, Save, Zoom, etc.");

    check_show_statusbar_ = new QCheckBox("Status Bar", this);
    check_show_statusbar_->setWhatsThis("Show the status bar at the bottom of the window with document information and current status.");

    check_horiz_tabs_ = new QCheckBox("Document Tabs At Top (requires app restart)", this);
    check_horiz_tabs_->setWhatsThis("Position document tabs at the top of the window. When unchecked, tabs appear at the bottom or side. Requires restarting the application to take effect.");

    btn_save_ = new QPushButton("Save", this);
    btn_save_->setDefault(true);
    btn_save_->setWhatsThis("Apply all settings changes and close the dialog.");

    btn_cancel_ = new QPushButton("Cancel", this);
    btn_cancel_->setWhatsThis("Discard all changes and close the dialog without saving.");

    // Layouts
    QFormLayout *form_layout = new QFormLayout;

    QGroupBox *group_show = new QGroupBox("Show", this);
    group_show->setWhatsThis("Controls which user interface elements are visible in the main window.");
    QVBoxLayout *group_layout = new QVBoxLayout;

    // First row: check_show_menu_
    group_layout->addWidget(check_show_menu_);

    // Second row: toolbar and statusbar
    QHBoxLayout *row_layout = new QHBoxLayout;
    row_layout->addWidget(check_show_toolbar_);
    row_layout->addWidget(check_show_statusbar_);
    group_layout->addLayout(row_layout);

    group_show->setLayout(group_layout);
    form_layout->addRow(group_show);

    // Checkboxes
    form_layout->addRow(check_center_);
    form_layout->addRow(check_restore_window_position_);
    form_layout->addRow(check_restore_documents_);
    form_layout->addRow(check_allow_oversize_);
    form_layout->addRow(check_horiz_tabs_);


    QHBoxLayout *layout_dpi = new QHBoxLayout;
    layout_dpi->addStretch();
    form_layout->addRow("DPI:", layout_dpi);
    layout_dpi->addWidget(spin_dpi_);

    QHBoxLayout *layout_border = new QHBoxLayout;
    layout_border->addStretch();
    form_layout->addRow("Border Margin:", layout_border);
    layout_border->addWidget(spin_border_margin_);

    // Max Recent Documents
    QHBoxLayout *layout_max_recent_documents = new QHBoxLayout;
    layout_max_recent_documents->addStretch();
    form_layout->addRow("Max Recent Documents:", layout_max_recent_documents);
    layout_max_recent_documents->addWidget(spin_max_recent_documents_);

    // Save Cadence
    QHBoxLayout *layout_save_cadence = new QHBoxLayout;
    layout_save_cadence->addStretch();
    layout_save_cadence->addWidget(spin_save_cadence_);
    form_layout->addRow("Save Modified Docs every (secs, 0 for never):", layout_save_cadence);

    // Theme
    /*
    QHBoxLayout *layout_theme = new QHBoxLayout;
    layout_theme->addStretch();
    layout_theme->addWidget(combo_theme_);
    form_layout->addRow("Theme:", layout_theme);
    */

    // Log Level
    QHBoxLayout *layout_log_level = new QHBoxLayout;
    layout_log_level->addStretch();
    layout_log_level->addWidget(combo_log_level_);
    form_layout->addRow("Log Level:", layout_log_level);

    // Save and Cancel buttons
    QHBoxLayout *buttons_layout = new QHBoxLayout;
    buttons_layout->addStretch();
    buttons_layout->addWidget(btn_save_);
    buttons_layout->addWidget(btn_cancel_);

    // Main layout
    QVBoxLayout *main_layout = new QVBoxLayout;
    main_layout->addLayout(form_layout);
    main_layout->addLayout(buttons_layout);

    setLayout(main_layout);

    // Adjust spin boxes to be narrower
    int spin_width = 50;
    spin_max_recent_documents_->setFixedWidth(spin_width);
    spin_save_cadence_->setFixedWidth(spin_width);

    adjustSize(); // Resize dialog to fit contents
}


void ConfigDialog::load_settings()
{
    // Load values from config_
    spin_dpi_->setValue(config_.dpi());
    spin_border_margin_->setValue(config_.border_margin());
    spin_max_recent_documents_->setValue(config_.max_recent_documents());
    spin_save_cadence_->setValue(config_.save_cadence_secs());

    //combo_theme_->setCurrentIndex(combo_theme_->findData(static_cast<int>(config_.theme())));
    combo_log_level_->setCurrentIndex(combo_log_level_->findData(static_cast<int>(config_.log_level())));

    check_center_->setChecked(config_.page_location() == PageLocation::Center);
    check_restore_window_position_->setChecked(config_.restore_window_position());
    check_restore_documents_->setChecked(config_.restore_documents());
    check_allow_oversize_->setChecked(config_.allow_oversize());
    check_show_menu_->setChecked(config_.show_menu());
    check_show_toolbar_->setChecked(config_.show_toolbar());
    check_show_statusbar_->setChecked(config_.show_status_bar());
    check_horiz_tabs_->setChecked(!config_.horiz_tabs());
}

void ConfigDialog::setup_connections()
{
    connect(btn_save_, &QPushButton::clicked, this, &ConfigDialog::save_settings);
    connect(btn_cancel_, &QPushButton::clicked, this, &ConfigDialog::cancel_settings);
}


void ConfigDialog::save_settings()
{
    // Validation
    if (spin_max_recent_documents_->value() <= 0) {
        QMessageBox::warning(this, "Validation Error", "Max Recent Documents must be greater than 0.");
        return;
    }

    if (spin_save_cadence_->value() < 0) {
        QMessageBox::warning(this, "Validation Error", "Save Cadence must be 0 or greater.");
        return;
    }

    // Apply settings to config_
    ConfigFileGroupSave group_saver(config_);

    config_.set_dpi(spin_dpi_->value());
    config_.set_border_margin(spin_border_margin_->value());
    config_.set_max_recent_documents(spin_max_recent_documents_->value());
    config_.set_save_cadence_secs(spin_save_cadence_->value());
    //config_.set_theme(static_cast<Theme>(combo_theme_->currentData().toInt()));
    config_.set_log_level(static_cast<LogLevel>(combo_log_level_->currentData().toInt()));
    config_.set_page_location(check_center_->isChecked() ? PageLocation::Center : PageLocation::Left);
    config_.set_restore_window_position(check_restore_window_position_->isChecked());
    config_.set_restore_documents(check_restore_documents_->isChecked());
    config_.set_allow_oversize(check_allow_oversize_->isChecked());
    config_.set_show_menu(check_show_menu_->isChecked());
    config_.set_show_toolbar(check_show_toolbar_->isChecked());
    config_.set_show_status_bar(check_show_statusbar_->isChecked());
    config_.set_horiz_tabs(!check_horiz_tabs_->isChecked());

    accept();
}


void ConfigDialog::cancel_settings()
{
    reject();
}