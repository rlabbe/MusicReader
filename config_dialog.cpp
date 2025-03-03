// config_dialog.cpp
#include "config_dialog.h"
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>


ConfigDialog::ConfigDialog(ConfigFile &config, QWidget *parent)
    : QDialog(parent), config_(config)
{
    setWindowTitle("Configuration Settings");
    setup_ui();
    load_settings();
    setup_connections();
}


void ConfigDialog::setup_ui()
{
    // Initialize widgets
    spin_border_margin_ = new QSpinBox(this);
    spin_border_margin_->setRange(0, 100);

    spin_max_recent_documents_ = new QSpinBox(this);
    spin_max_recent_documents_->setRange(1, 100);

    spin_page_view_count_ = new QSpinBox(this);
    spin_page_view_count_->setRange(1, 5);

    combo_theme_ = new QComboBox(this);
    combo_theme_->addItem("Dark", static_cast<int>(Theme::Dark));
    combo_theme_->addItem("Light", static_cast<int>(Theme::Light));

    combo_log_level_ = new QComboBox(this);
    combo_log_level_->addItem("Normal", static_cast<int>(LogLevel::Normal));
    combo_log_level_->addItem("Diagnostic", static_cast<int>(LogLevel::Diagnostic));

    check_restore_window_position_ = new QCheckBox("Restore Window Position", this);
    check_restore_documents_ = new QCheckBox("Restore Documents", this);
    check_zoom_to_content_ = new QCheckBox("Zoom to Content", this);
    check_allow_oversize_ = new QCheckBox("Allow Oversize", this);
    check_show_menu_ = new QCheckBox("Show Menu Bar", this);

    edit_music_directory_ = new QLineEdit(this);
    btn_browse_ = new QPushButton("Browse...", this);

    btn_save_ = new QPushButton("Save", this);
    btn_save_->setDefault(true);
    btn_cancel_ = new QPushButton("Cancel", this);

    // Layouts
    QFormLayout *form_layout = new QFormLayout;


    QHBoxLayout *layout_border = new QHBoxLayout;
    layout_border->addStretch();
    layout_border->addWidget(spin_border_margin_);
    form_layout->addRow("Border Margin:", layout_border);

    // Max Recent Documents
    QHBoxLayout *layout_max_recent_documents = new QHBoxLayout;
    layout_max_recent_documents->addStretch();
    layout_max_recent_documents->addWidget(spin_max_recent_documents_);
    form_layout->addRow("Max Recent Documents:", layout_max_recent_documents);

    // Page View Count
    QHBoxLayout *layout_page_view_count = new QHBoxLayout;
    layout_page_view_count->addStretch();
    layout_page_view_count->addWidget(spin_page_view_count_);
    form_layout->addRow("Page View Count:", layout_page_view_count);


    // Theme
    QHBoxLayout *layout_theme = new QHBoxLayout;
    layout_theme->addStretch();
    layout_theme->addWidget(combo_theme_);
    form_layout->addRow("Theme:", layout_theme);

    // Log Level
    QHBoxLayout *layout_log_level = new QHBoxLayout;
    layout_log_level->addStretch();
    layout_log_level->addWidget(combo_log_level_);
    form_layout->addRow("Log Level:", layout_log_level);

    // Checkboxes
    form_layout->addRow(check_restore_window_position_);
    form_layout->addRow(check_restore_documents_);
    form_layout->addRow(check_zoom_to_content_);
    form_layout->addRow(check_allow_oversize_);
    form_layout->addRow(check_show_menu_);

    // Music Directory with Browse button
    QHBoxLayout *layout_music_directory = new QHBoxLayout;
    layout_music_directory->addWidget(edit_music_directory_);
    layout_music_directory->addWidget(btn_browse_);
    form_layout->addRow("Music Directory:", layout_music_directory);

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
    spin_page_view_count_->setFixedWidth(spin_width);

    adjustSize(); // Resize dialog to fit contents
}


void ConfigDialog::load_settings()
{
    // Load values from config_
    spin_border_margin_->setValue(config_.border_margin());
    spin_max_recent_documents_->setValue(config_.max_recent_documents());
    spin_page_view_count_->setValue(config_.page_view_count());

    combo_theme_->setCurrentIndex(combo_theme_->findData(static_cast<int>(config_.theme())));
    combo_log_level_->setCurrentIndex(combo_log_level_->findData(static_cast<int>(config_.log_level())));

    check_restore_window_position_->setChecked(config_.restore_window_position());
    check_restore_documents_->setChecked(config_.restore_documents());
    check_zoom_to_content_->setChecked(config_.zoom_to_content());
    check_allow_oversize_->setChecked(config_.allow_oversize());
    check_show_menu_->setChecked(config_.show_menu());

    edit_music_directory_->setText(QString::fromStdString(config_.music_directory().string()));
}

void ConfigDialog::setup_connections()
{
    connect(btn_browse_, &QPushButton::clicked, this, &ConfigDialog::browse_music_directory);
    connect(btn_save_, &QPushButton::clicked, this, &ConfigDialog::save_settings);
    connect(btn_cancel_, &QPushButton::clicked, this, &ConfigDialog::cancel_settings);
}

void ConfigDialog::browse_music_directory()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Music Directory", edit_music_directory_->text());
    if (!dir.isEmpty())
    {
        edit_music_directory_->setText(dir);
    }
}

void ConfigDialog::save_settings()
{
    // Validation
    if (spin_max_recent_documents_->value() <= 0)
    {
        QMessageBox::warning(this, "Validation Error", "Max Recent Documents must be greater than 0.");
        return;
    }

    if (spin_page_view_count_->value() < 1 || spin_page_view_count_->value() > 5)
    {
        QMessageBox::warning(this, "Validation Error", "Page View Count must be between 1 and 5.");
        return;
    }


    if (edit_music_directory_->text().isEmpty())
    {
        QMessageBox::warning(this, "Validation Error", "Music Directory cannot be empty.");
        return;
    }

    // Apply settings to config_
    ConfigFileGroupSave group_saver(config_);

    config_.set_border_margin(spin_border_margin_->value());
    config_.set_max_recent_documents(spin_max_recent_documents_->value());
    config_.set_page_view_count(spin_page_view_count_->value());
    config_.set_theme(static_cast<Theme>(combo_theme_->currentData().toInt()));
    config_.set_log_level(static_cast<LogLevel>(combo_log_level_->currentData().toInt()));
    config_.set_restore_window_position(check_restore_window_position_->isChecked());
    config_.set_restore_documents (check_restore_documents_->isChecked());
    config_.set_zoom_to_content(check_zoom_to_content_->isChecked());
    config_.set_allow_oversize(check_allow_oversize_->isChecked());
    config_.set_show_menu(check_show_menu_->isChecked());
    config_.set_music_directory(std::filesystem::path(edit_music_directory_->text().toStdString()));

    accept(); // Close dialog with Accepted status
}

void ConfigDialog::cancel_settings()
{
    reject(); // Close dialog with Rejected status
}
