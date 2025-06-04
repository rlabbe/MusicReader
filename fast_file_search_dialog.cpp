#include "fast_file_search_dialog.h"
#include "logger.h"
#include <QThread>
#include <QDirIterator>
#include <filesystem>
#include "qt_utils.h"


static std::u8string to_lower(const std::u8string &str)
{
    QString qstr = QString::fromUtf8(reinterpret_cast<const char *>(str.c_str()));
    return reinterpret_cast<const char8_t *>(qstr.toLower().toUtf8().constData());
}


static std::u8string strip_accents(const std::u8string &text)
{
    QString qtext = QString::fromUtf8(reinterpret_cast<const char *>(text.c_str())).normalized(QString::NormalizationForm_D);

    // Remove non-spacing marks (accents)
    QRegularExpression regex("[\\p{Mn}]");
    qtext.remove(regex);

    return reinterpret_cast<const char8_t *>(qtext.toUtf8().constData());
}


static bool human_search(const std::u8string &search_term, const std::u8string &search_string)
{
    std::u8string normalized_search_term = strip_accents(to_lower(search_term));
    std::u8string normalized_search_string = strip_accents(to_lower(search_string));

    return normalized_search_string.find(normalized_search_term) != std::u8string::npos;
}


static std::u8string human_size(long size)
{
    const char8_t *units[] = { u8"B", u8"KB", u8"MB", u8"GB" };
    int unit_index = 0;

    while (size >= 1024 && unit_index < 3) {
        size /= 1024;
        ++unit_index;
    }
    std::u8string result = reinterpret_cast<const char8_t *>(std::to_string(size).c_str());
    result += u8" ";
    result += units[unit_index];
    return result;
}

SortableTableWidgetItem::SortableTableWidgetItem(int sort_value, const QString &text)
    : QTableWidgetItem(text), sort_value_(sort_value)
{
}


bool SortableTableWidgetItem::operator<(const QTableWidgetItem &other) const
{
    auto *other_item = dynamic_cast<const SortableTableWidgetItem *>(&other);
    return other_item ? sort_value_ < other_item->sort_value_ : QTableWidgetItem::operator<(other);
}


FastFileSearchDialog::FastFileSearchDialog(QWidget *parent, const std::filesystem::path &directory_path, const QRect &size)
    : QDialog(parent)
{
    instance_ = this;
    std::filesystem::path path = directory_path;

    // Wait until the file-loading thread signals completion
    std::unique_lock<std::mutex> lk(files_mutex_);
    files_cv_.wait(lk, [] { return files_ready_; });

    if (path != path_) {
        // This is slightly inefficient but should only occur if the user manually edits the music_directory in settings.
        path_ = path;
        files_.clear(); // Trigger a refresh of the files
    }

    if (!file_ending_.isEmpty())
        file_ending_ = file_ending_.toLower();

    selected_items_.clear();
    open_path_.clear(); // Used to browse to a directory

    init_ui(size);

    if (files_.empty())
        update_files();

    display_files(files_);

    connect(&UpdateSignal::instance(), &UpdateSignal::filesUpdated, this, &FastFileSearchDialog::instance_update_files);
}


void FastFileSearchDialog::init_ui(const QRect &size)
{
    layout_ = new QVBoxLayout(this);

    // Search field and radio buttons layout
    top_layout_ = new QHBoxLayout();
    label_ = new QLabel(
        "search items separated by spaces will match either path or filename (f1 for help):", this);
    layout_->addWidget(label_);

    search_field_ = new QLineEdit(this);
    search_field_->setPlaceholderText(
        "gla ko is enough to match philip glass\\Koyaanisqatsi.pdf");
    connect(search_field_, &QLineEdit::textChanged, this, &FastFileSearchDialog::on_search);
    top_layout_->addWidget(search_field_);


    auto update_button = new QPushButton();
    update_button->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    update_button->setToolTip("refresh all files");
    update_button->setFocusPolicy(Qt::NoFocus);
    connect(update_button, &QPushButton::clicked, this, &FastFileSearchDialog::update_files);
    top_layout_->addWidget(update_button);


    auto browse_button = new QPushButton("...", this);
    size_button(browse_button);
    browse_button->setToolTip("select music directory");
    browse_button->setFocusPolicy(Qt::NoFocus);
    connect(browse_button, &QPushButton::clicked, this, &FastFileSearchDialog::on_select_directory);
    top_layout_->addWidget(browse_button);


    auto open_button = new QPushButton("Open Dialog...", this);
    size_button(open_button);
    open_button->setToolTip("use Windows open file dialog...");
    open_button->setFocusPolicy(Qt::NoFocus);
    connect(open_button, &QPushButton::clicked, this, &FastFileSearchDialog::on_open_file_dialog);
    top_layout_->addWidget(open_button);

    layout_->addLayout(top_layout_);

    // widget for displaying files
    file_table_ = new QTableWidget(0, 3, this);
    file_table_->setHorizontalHeaderLabels({ "Name", "Date Modified", "Size" });
    file_table_->setSelectionBehavior(QTableWidget::SelectRows);
    file_table_->setSortingEnabled(true);
    file_table_->setShowGrid(false);
    file_table_->sortByColumn(0, Qt::AscendingOrder);
    connect(file_table_, &QTableWidget::itemDoubleClicked, this, &FastFileSearchDialog::on_item_double_click);
    layout_->addWidget(file_table_);

    // Add context menu to file table
    file_table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(file_table_, &QWidget::customContextMenuRequested, this, &FastFileSearchDialog::show_context_menu);

    QPushButton *okay_button = new QPushButton("OK", this);
    connect(okay_button, &QPushButton::clicked, this, &FastFileSearchDialog::accept);
    okay_button->setDefault(true);
    layout_->addWidget(okay_button);

    QHeaderView *h_header = file_table_->horizontalHeader();
    QHeaderView *v_header = file_table_->verticalHeader();

    h_header->setSectionResizeMode(0, QHeaderView::Stretch);
    h_header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    h_header->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    v_header->setVisible(false);
    v_header->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Install event filter to catch What's This mode events
    QApplication::instance()->installEventFilter(this);

    setWindowModality(Qt::ApplicationModal);
    set_title();

    try {
        if (size.x() >= 0 && size.y() >= 0)
            setGeometry(size);
    } catch (...) {
        logger::warning("FastSearchDialog size not set in config file, using default size");
        resize(600, 600);
    }

    // don't display until app asks us to
    ensure_window_is_visible(this, true);
}


void FastFileSearchDialog::initialize_data(const std::filesystem::path &directory)
{
    // this must be called before the class is created. It sets up the directory 
    // watcher to monitor the directory for changes, and reads the files in the
    // directory, if any.
    if (watcher_) {
        logger::error("FastFileSearchDialog::initialize_data called more than once");
        return;
    }

    path_ = directory;
    watcher_ = new DirectoryWatcher(file_ending_);
    connect(watcher_, &DirectoryWatcher::file_changed, &FastFileSearchDialog::class_file_changed);

    if (!directory.empty())
        watcher_->start(QString::fromStdU16String(path_.u16string()));

    std::thread([] {
        auto files = find_files(FastFileSearchDialog::path_, FastFileSearchDialog::file_ending_);
        {
            std::lock_guard lk(files_mutex_);
            files_ = std::move(files);
            files_ready_ = true;
        }
        files_cv_.notify_one();
    }).detach();
}


void FastFileSearchDialog::show_dialog()
{
    file_table_->clearSelection();
    search_field_->clear();
    search_field_->setFocus();

    show();
    exec();
}


void FastFileSearchDialog::on_search()
{
    QString search_text = search_field_->text().trimmed();
    if (search_text.isEmpty()) {
        display_files(files_);
        return;
    }

    QStringList search_terms = search_text.split(' ', Qt::SkipEmptyParts);

    QStringList filtered_files;
    for (const auto &file : files_) {
        bool match = std::all_of(search_terms.begin(), search_terms.end(), [&](const QString &term) {
            std::u8string term_u8 = reinterpret_cast<const char8_t *>(term.toUtf8().constData());
            std::u8string file_u8 = reinterpret_cast<const char8_t *>(file.toUtf8().constData());
            return human_search(term_u8, file_u8);
        });

        if (match)
            filtered_files.append(file);
    }

    display_files(filtered_files);
}


void FastFileSearchDialog::display_files(const QStringList &file_paths, bool resize)
{
    file_table_->setSortingEnabled(false);
    file_table_->setRowCount(0);

    for (int i = 0; i < file_paths.size(); ++i) {
        QFileInfo file_info(file_paths[i]);
        QString relative_path = QDir(QString::fromStdU16String(path_.u16string())).relativeFilePath(file_paths[i]); // Strip directory
        QString date_text = QLocale().toString(file_info.lastModified(), QLocale::ShortFormat);
        QString size_text = QString::fromUtf8(reinterpret_cast<const char *>(human_size(file_info.size()).c_str()));

        // Use string for path sorting
        auto *path_item = new QTableWidgetItem(relative_path);

        // Use timestamp for date sorting
        auto *date_item = new SortableTableWidgetItem(file_info.lastModified().toSecsSinceEpoch(), date_text);

        // Use actual file size for size sorting
        auto *size_item = new SortableTableWidgetItem(file_info.size(), size_text);

        path_item->setFlags(path_item->flags() ^ Qt::ItemIsEditable);
        date_item->setFlags(date_item->flags() ^ Qt::ItemIsEditable);
        size_item->setFlags(size_item->flags() ^ Qt::ItemIsEditable);

        int row_position = file_table_->rowCount();
        file_table_->insertRow(row_position);
        file_table_->setItem(row_position, 0, path_item);
        file_table_->setItem(row_position, 1, date_item);
        file_table_->setItem(row_position, 2, size_item);
    }

    file_table_->setSortingEnabled(true);

    // Only resize columns 1 and 2 (date and size), never column 0 (name)
    // This preserves the stretch behavior for column 0
    if (resize) {
        // Resize only the date and size columns
        file_table_->resizeColumnToContents(1);
        file_table_->resizeColumnToContents(2);
    }

    // select if only one file so user can just press return
    // to open the single file they found
    if (file_paths.size() == 1)
        file_table_->selectAll();
}


void FastFileSearchDialog::class_file_changed()
{
    instance_->files_ = instance_->find_files(path_, file_ending_);
    emit UpdateSignal::instance().filesUpdated();
}


void FastFileSearchDialog::instance_update_files()
{
    display_files(files_, true);
}


void FastFileSearchDialog::update_files()
{
    files_ = find_files(path_, file_ending_);
    display_files(files_, true);
    set_title();
}


void FastFileSearchDialog::on_select_directory()
{
    QString new_search_path = QFileDialog::getExistingDirectory(this, "Select Search Path", QString::fromStdU16String(path_.u16string()));
    if (!new_search_path.isEmpty()) {
        directory_changed(std::filesystem::path(new_search_path.toStdU16String()), this);
    }
}


void FastFileSearchDialog::on_open_file_dialog()
{
    selected_items_.clear();
    selected_items_ = QFileDialog::getOpenFileNames(this, "Open Files", QString::fromStdU16String(path_.u16string()), "PDF Files (*.pdf)");
    accept();
}


void FastFileSearchDialog::accept()
{
    hide();
}


void FastFileSearchDialog::size_button(QPushButton *button)
{
    button->setFixedWidth(button->fontMetrics().boundingRect(button->text()).width() + 10);
}


void FastFileSearchDialog::show_context_menu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *browse_action = menu.addAction("Browse to Directory...");
    connect(browse_action, &QAction::triggered, this, &FastFileSearchDialog::browse_to_directory);
    menu.exec(file_table_->mapToGlobal(pos));
}


void FastFileSearchDialog::browse_to_directory()
{
    QList<QTableWidgetItem *> selected_items = file_table_->selectedItems();
    if (!selected_items.isEmpty()) {
        QString file_path = QString::fromStdU16String(path_.u16string()) + "/" + selected_items[0]->text();
        QString directory_path = QFileInfo(file_path).absolutePath();
        on_open_file_dialog();
    }
}


void FastFileSearchDialog::on_item_double_click(QTableWidgetItem *item)
{
    selected_items_.clear();
    selected_items_.append(QString::fromStdU16String(path_.u16string()) + "/" + item->text());
    accept();
}


void FastFileSearchDialog::show_help()
{
    QDesktopServices::openUrl(QUrl(":/MusicReader/help/fast_search_help.html"));
}


std::pair<std::vector<std::filesystem::path>, std::filesystem::path> FastFileSearchDialog::selected_files() const
{
    std::vector<std::filesystem::path> selected_paths;
    std::filesystem::path base_path = path_;

    for (const QModelIndex &index : file_table_->selectionModel()->selectedRows()) {
        auto *item = file_table_->item(index.row(), 0); // Get file name from first column
        if (item) {
            std::filesystem::path full_path = base_path / std::filesystem::path(item->text().toStdU16String());
            selected_paths.push_back(full_path.lexically_normal()); // Normalize the path
        }
    }

    return { selected_paths, base_path };
}


void FastFileSearchDialog::reject()
{
    // handle esc or whatever; clear the selection (if any) so the app 
    // doesn't try to open the file, and of course hide the dialog.
    file_table_->clearSelection();
    hide();
}


void FastFileSearchDialog::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}


void FastFileSearchDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F1) {
        show_help();
        event->accept();
    } else
        QDialog::keyPressEvent(event);
}


bool FastFileSearchDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::EnterWhatsThisMode) {
        show_help();
        return true;
    }
    return QDialog::eventFilter(object, event);
}

#include <iostream>

QStringList FastFileSearchDialog::find_files(const std::filesystem::path &path, QString &file_ending)
{
    QStringList file_paths;
    QDirIterator it(QString::fromStdU16String(path.u16string()), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString file_path = it.next();
        if (file_ending.isEmpty() || file_path.endsWith(file_ending, Qt::CaseInsensitive))
            file_paths.append(file_path);
    }
    return file_paths;
}


void FastFileSearchDialog::directory_changed(const std::filesystem::path &new_search_path, FastFileSearchDialog *self)
{
    if (new_search_path.empty()) return;

    path_ = new_search_path;

    if (watcher_)
        watcher_->start(QString::fromStdU16String(path_.u16string()));

    if (self)
        self->update_files();
}


void FastFileSearchDialog::set_title()
{
    if (!path_.empty())
        setWindowTitle(QString("Fast File Search: %1").arg(QString::fromStdU16String(path_.u16string())));
    else
        setWindowTitle("Fast File Search");
}