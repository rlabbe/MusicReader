#pragma once

#include <QtWidgets>

class MusicReader;

// Dialog to create a book from musescore files
//
// The user picks an .mrb file, which names the documents and the output PDF.
// Create exports every document with MuseScore, merges them, and writes the
// outline and the .mrd sidecar. Both checkboxes are stored in ConfigFile, so
// the next open of the dialog starts with the same choices.
class CreateBookDialog : public QDialog {
    Q_OBJECT

public:
    explicit CreateBookDialog(MusicReader* parent);

protected:
    void reject() override;

private slots:
    void on_browse();
    void on_create();

private:
    void setup_ui();
    void set_busy(bool busy);
    void disable_for_missing_program(const QString& status, const QString& message);
    void report_failure(const QString& message);

    MusicReader* main_window_;
    QLineEdit* spec_edit_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    QCheckBox* overwrite_checkbox_ = nullptr;
    QCheckBox* open_after_checkbox_ = nullptr;
    QCheckBox* save_exports_checkbox_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPushButton* create_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;

    // Set by the progress callback when the user presses Cancel.
    bool cancelled_ = false;

    // True while build_book runs, so reject can ignore Escape.
    bool building_ = false;
};
