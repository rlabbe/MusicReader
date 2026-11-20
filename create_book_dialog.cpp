#include "create_book_dialog.h"

#include <QTimer>

#include "book.h"
#include "config_file.h"
#include "logger.h"
#include "music_reader.h"

namespace {
QString to_qstring(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}
} // namespace

CreateBookDialog::CreateBookDialog(MusicReader* parent)
    : QDialog(parent)
    , main_window_(parent)
{
    TRACE_FUNCTION;

    setWindowTitle("Create Book");
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
                                    "The MuseScore program was not found:\n\n" + to_qstring(program) +
                                        "\n\nSet its path in File, Settings, then try again.");
    }
}

void CreateBookDialog::setup_ui()
{
    TRACE_FUNCTION;

    const ConfigFile& config = main_window_->config();

    spec_edit_ = new QLineEdit(this);
    spec_edit_->setPlaceholderText("Path to a book .mrb file");

    browse_button_ = new QPushButton("&Browse...", this);

    auto* file_row = new QHBoxLayout;
    file_row->addWidget(new QLabel("Book file:", this));
    file_row->addWidget(spec_edit_, 1);
    file_row->addWidget(browse_button_);

    overwrite_checkbox_ = new QCheckBox("Overwrite without asking", this);
    overwrite_checkbox_->setChecked(config.musescore_export_overwrite());

    open_after_checkbox_ = new QCheckBox("Open after converting", this);
    open_after_checkbox_->setChecked(config.musescore_export_open_after());

    save_exports_checkbox_ = new QCheckBox("Save individual PDFs", this);
    save_exports_checkbox_->setChecked(config.book_save_exports());
    save_exports_checkbox_->setToolTip("Keep each converted document as its own PDF in the output directory");

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(this);
    create_button_ = buttons->addButton("&Create", QDialogButtonBox::AcceptRole);
    cancel_button_ = buttons->addButton(QDialogButtonBox::Cancel);
    create_button_->setEnabled(false);

    create_button_->setDefault(true);
    browse_button_->setAutoDefault(false);
    cancel_button_->setAutoDefault(false);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(file_row);
    layout->addWidget(overwrite_checkbox_);
    layout->addWidget(open_after_checkbox_);
    layout->addWidget(save_exports_checkbox_);
    layout->addWidget(status_label_);
    layout->addWidget(buttons);

    connect(browse_button_, &QPushButton::clicked, this, &CreateBookDialog::on_browse);
    connect(create_button_, &QPushButton::clicked, this, &CreateBookDialog::on_create);
    connect(cancel_button_, &QPushButton::clicked, this, &CreateBookDialog::reject);
    connect(spec_edit_, &QLineEdit::textChanged, this, [this]() {
        create_button_->setEnabled(!building_ && !spec_edit_->text().isEmpty());
    });

    // Saved as the user toggles them, so the choice survives a cancel.
    connect(overwrite_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        main_window_->config().set_musescore_export_overwrite(checked);
    });
    connect(open_after_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        main_window_->config().set_musescore_export_open_after(checked);
    });
    connect(save_exports_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        main_window_->config().set_book_save_exports(checked);
    });

    resize(600, 180);
}

void CreateBookDialog::reject()
{
    if (building_)
        return;

    QDialog::reject();
}

void CreateBookDialog::on_browse()
{
    TRACE_FUNCTION;

    std::filesystem::path start_directory = main_window_->config().music_directory();
    if (start_directory.empty())
        start_directory = std::filesystem::path(QDir::homePath().toStdU16String());

    const QString filename =
        QFileDialog::getOpenFileName(this, "Select a Book File", to_qstring(start_directory), "Book Files (*.mrb)");

    if (!filename.isEmpty()) {
        spec_edit_->setText(filename);
        create_button_->setFocus();
    }
}

void CreateBookDialog::on_create()
{
    TRACE_FUNCTION;

    const std::filesystem::path spec_file(spec_edit_->text().toStdU16String());
    if (!std::filesystem::exists(spec_file)) {
        report_failure("File not found:\n" + spec_edit_->text());
        return;
    }

    BookSpec spec;
    std::string error;
    if (!parse_book_spec(spec_file, spec, error)) {
        report_failure(QString::fromStdString(error));
        return;
    }

    const std::filesystem::path musescore = main_window_->config().musescore_path();
    if (musescore.empty() || !std::filesystem::exists(musescore)) {
        report_failure("MuseScore program not found. Set its path in File, Settings.");
        return;
    }

    const bool overwrite = overwrite_checkbox_->isChecked();
    const BookConfirmFn confirm = [this, overwrite](const std::vector<std::filesystem::path>& existing_files) {
        if (overwrite)
            return true;

        const size_t maximum_shown = 12;

        std::string text = "These files already exist and will be replaced:\n\n";
        size_t shown = 0;
        for (const auto& path : existing_files) {
            if (shown == maximum_shown) {
                text += "... and " + std::to_string(existing_files.size() - shown) + " more\n";
                break;
            }
            text += path.string() + "\n";
            ++shown;
        }
        text += "\nContinue?";

        if (main_window_->display_query(text))
            return true;

        cancelled_ = true;
        return false;
    };

    // Not shown until the first progress report, so the confirmation above is
    // not buried under it.
    QProgressDialog progress("Building the book...", "Cancel", 0, static_cast<int>(spec.entries.size()), this);
    progress.setWindowTitle("Create Book");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);

    cancelled_ = false;
    const BookProgressFn report = [&progress, this](int current, int total, const std::string& status) {
        progress.setLabelText(QString::fromStdString(status));
        progress.setMaximum(total);
        progress.setValue(current);

        if (progress.wasCanceled()) {
            cancelled_ = true;
            return false;
        }
        return true;
    };

    set_busy(true);
    status_label_->setText("Building...");
    const bool built = build_book(spec, musescore, save_exports_checkbox_->isChecked(), confirm, report, error);
    set_busy(false);
    progress.close();

    if (!built) {
        status_label_->clear();
        if (!cancelled_)
            report_failure(QString::fromStdString(error));
        return;
    }

    status_label_->clear();
    logger::info("Created book {}", spec.output.string());

    if (open_after_checkbox_->isChecked())
        main_window_->open_pdf_in_tab(spec.output);

    accept();
}

void CreateBookDialog::set_busy(bool busy)
{
    building_ = busy;

    spec_edit_->setEnabled(!busy);
    browse_button_->setEnabled(!busy);
    overwrite_checkbox_->setEnabled(!busy);
    open_after_checkbox_->setEnabled(!busy);
    save_exports_checkbox_->setEnabled(!busy);
    cancel_button_->setEnabled(!busy);
    create_button_->setEnabled(!busy && !spec_edit_->text().isEmpty());

    if (busy) {
        setCursor(Qt::BusyCursor);
    } else {
        unsetCursor();
    }
}

void CreateBookDialog::disable_for_missing_program(const QString& status, const QString& message)
{
    spec_edit_->setEnabled(false);
    browse_button_->setEnabled(false);
    overwrite_checkbox_->setEnabled(false);
    open_after_checkbox_->setEnabled(false);
    save_exports_checkbox_->setEnabled(false);
    create_button_->setEnabled(false);
    status_label_->setText(status);

    // Shown after the dialog is on screen, so the box is not orphaned.
    QTimer::singleShot(0, this, [this, message]() {
        QMessageBox::warning(this, "Create Book", message);
    });
}

void CreateBookDialog::report_failure(const QString& message)
{
    logger::error(message.toStdString());
    main_window_->display_error_message(message.toStdString());
}
