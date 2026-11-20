#pragma once

#include <QtWidgets>
#include <QProcess>
#include <filesystem>

class MusicReader;

//  Export MuseScore to PDF dialog
//
// The user picks an .mscz file; Export runs the MuseScore command line
// converter on it and writes a PDF beside the .mscz. The program that runs is
// the path stored in ConfigFile and set in the Settings dialog. The checkboxes
// are stored in ConfigFile too, so the next open of the dialog starts with the
// same choices.
//
// The conversion runs as a child process, so the window stays responsive. The
// dialog closes only after the child exits, and reject is ignored while it
// runs, so a half written PDF cannot be left behind by a stray Escape.
class ExportMuseScoreDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportMuseScoreDialog(MusicReader* parent);

protected:
    void reject() override;

private slots:
    void on_browse();
    void on_export();
    void on_process_finished(int exit_code, QProcess::ExitStatus status);
    void on_process_error(QProcess::ProcessError error);

private:
    void setup_ui();
    void set_busy(bool busy);
    void disable_for_missing_program(const QString& status, const QString& message);
    void start_conversion(const std::filesystem::path& input, const std::filesystem::path& output);
    void finish_run();
    void report_failure(const QString& message);

    MusicReader* main_window_;
    QLineEdit* path_edit_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    QCheckBox* overwrite_checkbox_ = nullptr;
    QCheckBox* open_after_checkbox_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPushButton* export_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QProcess* process_ = nullptr;

    // Output path of the running conversion, needed by the "open after" step.
    std::filesystem::path pending_output_;
};
