#include "export_musescore_dialog.h"

#include <QTimer>

#include "config_file.h"
#include "logger.h"
#include "music_reader.h"

namespace {
// Longest run of child output copied into the error dialog.
constexpr int MAX_REPORTED_OUTPUT = 800;

QString to_qstring(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

// MuseScore exits with the converter error code from src/converter/convertercodes.h.
QString converter_error_reason(int code)
{
    switch (code) {
        case 1301: return "the conversion job file could not be opened";
        case 1302: return "the conversion job file could not be parsed";
        case 1310: return "the output file type is not supported";
        case 1320: return "the score could not be loaded, or it comes from a newer MuseScore";
        case 1330: return "the output file could not be opened for writing";
        case 1331: return "the output file could not be written";
        default: return "unrecognised error";
    }
}
} // namespace

ExportMuseScoreDialog::ExportMuseScoreDialog(MusicReader* parent)
    : QDialog(parent)
    , main_window_(parent)
{
    TRACE_FUNCTION;

    setWindowTitle("Export MuseScore to PDF");
    setModal(true);
    setup_ui();

    // The path is checked here rather than at startup, so a program on a drive
    // that is offline right now is not mistaken for an uninstalled one.
    const std::filesystem::path program = main_window_->config().musescore_path();
    if (program.empty()) {
        disable_for_missing_program("No MuseScore program is set. Set its path in File, Settings.",
                                    "No MuseScore program is set.\n\nSet its path in File, Settings, then try again.");
    } else if (!std::filesystem::exists(program)) {
        logger::warning("MuseScore program not found: " + program.string());
        disable_for_missing_program("MuseScore program not found. Set its path in File, Settings.",
                                    "The MuseScore program was not found:\n\n" + to_qstring(program)
                                    + "\n\nSet its path in File, Settings, then try again.");
    }
}

void ExportMuseScoreDialog::disable_for_missing_program(const QString& status, const QString& message)
{
    path_edit_->setEnabled(false);
    browse_button_->setEnabled(false);
    overwrite_checkbox_->setEnabled(false);
    open_after_checkbox_->setEnabled(false);
    export_button_->setEnabled(false);
    status_label_->setText(status);

    // Shown after the dialog is on screen, so the box is not orphaned.
    QTimer::singleShot(0, this, [this, message]() {
        QMessageBox::warning(this, "Export MuseScore to PDF", message);
    });
}

void ExportMuseScoreDialog::setup_ui()
{
    TRACE_FUNCTION;

    const ConfigFile& config = main_window_->config();

    path_edit_ = new QLineEdit(this);
    path_edit_->setPlaceholderText("Path to a MuseScore .mscz file");

    browse_button_ = new QPushButton("&Browse...", this);

    auto* file_row = new QHBoxLayout;
    file_row->addWidget(new QLabel("MuseScore file:", this));
    file_row->addWidget(path_edit_, 1);
    file_row->addWidget(browse_button_);

    overwrite_checkbox_ = new QCheckBox("Overwrite without asking", this);
    overwrite_checkbox_->setChecked(config.musescore_export_overwrite());

    open_after_checkbox_ = new QCheckBox("Open after converting", this);
    open_after_checkbox_->setChecked(config.musescore_export_open_after());

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(this);
    export_button_ = buttons->addButton("&Export", QDialogButtonBox::AcceptRole);
    cancel_button_ = buttons->addButton(QDialogButtonBox::Cancel);
    export_button_->setEnabled(false);

    // Return goes to Export. Browse and Cancel keep focus but do not take the
    // default, so a Browse followed by Return starts the export.
    export_button_->setDefault(true);
    browse_button_->setAutoDefault(false);
    cancel_button_->setAutoDefault(false);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(file_row);
    layout->addWidget(overwrite_checkbox_);
    layout->addWidget(open_after_checkbox_);
    layout->addWidget(status_label_);
    layout->addWidget(buttons);

    connect(browse_button_, &QPushButton::clicked, this, &ExportMuseScoreDialog::on_browse);
    connect(export_button_, &QPushButton::clicked, this, &ExportMuseScoreDialog::on_export);
    connect(cancel_button_, &QPushButton::clicked, this, &ExportMuseScoreDialog::reject);
    connect(path_edit_, &QLineEdit::textChanged, this, [this]() {
        export_button_->setEnabled(process_ == nullptr && !path_edit_->text().isEmpty());
    });

    // Saved as the user toggles them, so the choice survives a cancel.
    connect(overwrite_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        main_window_->config().set_musescore_export_overwrite(checked);
    });
    connect(open_after_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        main_window_->config().set_musescore_export_open_after(checked);
    });

    resize(600, 180);
}

void ExportMuseScoreDialog::reject()
{
    // Ignore Escape and the close button while the converter runs, otherwise
    // the process is killed mid write and leaves a truncated PDF.
    if (process_)
        return;

    QDialog::reject();
}

void ExportMuseScoreDialog::on_browse()
{
    TRACE_FUNCTION;

    std::filesystem::path start_directory = main_window_->config().music_directory();
    if (start_directory.empty())
        start_directory = std::filesystem::path(QDir::homePath().toStdU16String());

    const QString filename = QFileDialog::getOpenFileName(
        this, "Select a MuseScore File", to_qstring(start_directory), "MuseScore Files (*.mscz *.mscx)");

    if (!filename.isEmpty()) {
        path_edit_->setText(filename);
        export_button_->setFocus();
    }
}

void ExportMuseScoreDialog::on_export()
{
    TRACE_FUNCTION;

    const std::filesystem::path input(path_edit_->text().toStdU16String());
    if (!std::filesystem::exists(input)) {
        report_failure("File not found:\n" + path_edit_->text());
        return;
    }

    std::filesystem::path output = input;
    output.replace_extension(".pdf");

    if (!overwrite_checkbox_->isChecked() && std::filesystem::exists(output)) {
        const QString question = "Overwrite this file?\n\n" + to_qstring(output);
        if (!main_window_->display_query(question.toStdString()))
            return;
    }

    start_conversion(input, output);
}

void ExportMuseScoreDialog::start_conversion(const std::filesystem::path& input, const std::filesystem::path& output)
{
    TRACE_FUNCTION;

    const std::filesystem::path executable = main_window_->config().musescore_path();
    if (executable.empty() || !std::filesystem::exists(executable)) {
        report_failure("MuseScore program not found. Set its path in File, Settings.");
        return;
    }

    pending_output_ = output;

    process_ = new QProcess(this);
    if (!output.parent_path().empty())
        process_->setWorkingDirectory(to_qstring(output.parent_path()));

    connect(process_, &QProcess::finished, this, &ExportMuseScoreDialog::on_process_finished);
    connect(process_, &QProcess::errorOccurred, this, &ExportMuseScoreDialog::on_process_error);

    set_busy(true);
    status_label_->setText("Converting...");

    const QStringList arguments { "-o", to_qstring(output), to_qstring(input) };
    process_->start(to_qstring(executable), arguments);
}

void ExportMuseScoreDialog::on_process_finished(int exit_code, QProcess::ExitStatus status)
{
    TRACE_FUNCTION;

    // Read the child before finish_run drops the process object.
    QString child_output = QString::fromLocal8Bit(process_->readAllStandardOutput());
    const QString child_error = QString::fromLocal8Bit(process_->readAllStandardError());
    if (!child_error.isEmpty())
        child_output += child_error;

    finish_run();

    if (status != QProcess::NormalExit || exit_code != 0) {
        QString message = QString("MuseScore failed with exit code %1: %2.")
                              .arg(exit_code)
                              .arg(converter_error_reason(exit_code));

        if (!child_output.trimmed().isEmpty()) {
            if (child_output.size() > MAX_REPORTED_OUTPUT)
                child_output = child_output.right(MAX_REPORTED_OUTPUT);
            message += "\n\nMuseScore output:\n" + child_output.trimmed();
        }

        report_failure(message);
        return;
    }

    if (!std::filesystem::exists(pending_output_)) {
        report_failure("MuseScore reported success but wrote no file:\n" + to_qstring(pending_output_));
        return;
    }

    if (open_after_checkbox_->isChecked())
        main_window_->open_pdf_in_tab(pending_output_);

    accept();
}

void ExportMuseScoreDialog::on_process_error(QProcess::ProcessError error)
{
    TRACE_FUNCTION;

    // The other error values also emit finished, which reports them.
    if (error != QProcess::FailedToStart)
        return;

    finish_run();
    report_failure("Could not start the MuseScore program. Check its path in this dialog.");
}

void ExportMuseScoreDialog::finish_run()
{
    QProcess* finished_process = process_;
    process_ = nullptr;

    if (finished_process)
        finished_process->deleteLater();

    status_label_->clear();
    set_busy(false);
}

void ExportMuseScoreDialog::set_busy(bool busy)
{
    path_edit_->setEnabled(!busy);
    browse_button_->setEnabled(!busy);
    overwrite_checkbox_->setEnabled(!busy);
    open_after_checkbox_->setEnabled(!busy);
    cancel_button_->setEnabled(!busy);
    export_button_->setEnabled(!busy && !path_edit_->text().isEmpty());

    if (busy) {
        setCursor(Qt::BusyCursor);
    } else {
        unsetCursor();
    }
}

void ExportMuseScoreDialog::report_failure(const QString& message)
{
    logger::error(message.toStdString());
    main_window_->display_error_message(message.toStdString());
}
