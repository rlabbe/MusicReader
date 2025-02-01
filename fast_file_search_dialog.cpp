#include "fast_file_search_dialog.h"


#include "fast_file_search_dialog.h"
#include "logger.h"
#include <QThread>
#include <QDirIterator>
#include <filesystem>
#include <iostream>
#include "qt_utils.h"


static std::string to_lower(const std::string &str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::string strip_accents(const std::string &text)
{
    QString qtext = QString::fromStdString(text).normalized(QString::NormalizationForm_D);

    // Remove non-spacing marks (accents)
    QRegularExpression regex("[\\p{Mn}]");
    qtext.remove(regex);

    return qtext.toStdString();
}

static bool human_search(const std::string &search_term, const std::string &search_string)
{
    std::string normalized_search_term = strip_accents(to_lower(search_term));
    std::string normalized_search_string = strip_accents(to_lower(search_string));

    return normalized_search_string.find(normalized_search_term) != std::string::npos;
}

static std::string human_size(long size)
{
    const char *units[] = { "B", "KB", "MB", "GB" };
    int unit_index = 0;

    while (size >= 1024 && unit_index < 3) {
        size /= 1024;
        ++unit_index;
    }
    return std::to_string(size) + " " + units[unit_index];
}

SortableTableWidgetItem::SortableTableWidgetItem(int sort_value, const QString &text)
    : QTableWidgetItem(text), sort_value_(sort_value)
{}

bool SortableTableWidgetItem::operator<(const QTableWidgetItem &other) const
{
    auto *other_item = dynamic_cast<const SortableTableWidgetItem *>(&other);
    return other_item ? sort_value_ < other_item->sort_value_ : QTableWidgetItem::operator<(other);
}

FastFileSearchDialog::FastFileSearchDialog(QWidget *parent, const std::string &directory_path, const QRect &size)
    : QDialog(parent)
{
    instance_ = this;
    QString path = QDir::toNativeSeparators(QString::fromStdString(directory_path));

    if (path != path_) {
        // This is slightly inefficient but should only occur if the user manually edits the music_directory in settings.
        path_ = path;
        files_.clear(); // Trigger a refresh of the files
    }

    if (!file_ending_.isEmpty()) {
        file_ending_ = file_ending_.toLower();
    }

    selected_items_.clear();
    open_path_.clear(); // Used to browse to a directory

    init_ui(size);

    if (files_.empty()) {
        update_files();
    }

    if (!watcher_) {
        watcher_ = new DirectoryWatcher(file_ending_);
        connect(watcher_, &DirectoryWatcher::file_changed, this, &FastFileSearchDialog::class_file_changed);
        watcher_->start(path_);
    }

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

    // Create the browse button
    browse_button_ = new QPushButton("...", this);
    size_button(browse_button_);
    top_layout_->addWidget(browse_button_);
    connect(browse_button_, &QPushButton::clicked, this, &FastFileSearchDialog::on_select_directory);


    // button to open file dialog
    open_button_ = new QPushButton("Open Dialog...", this);
    size_button(open_button_);
    connect(open_button_, &QPushButton::clicked, this, &FastFileSearchDialog::on_open_file_dialog);
    top_layout_->addWidget(open_button_);

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
        if (size.x() >= 0 && size.y() >= 0) {
            setGeometry(size);
        }
    } catch (...) {
        logger::log_warning("FastSearchDialog size not set in config file, using default size");
        resize(600, 600);
    }

    // don't display until app asks us to
    ensure_window_is_visible(this, true);
}


void FastFileSearchDialog::initialize_watcher(const std::string &directory)
{
    if (watcher_) return;

    path_ = QString::fromStdString(directory);
    watcher_ = new DirectoryWatcher(file_ending_);
    connect(watcher_, &DirectoryWatcher::file_changed, &FastFileSearchDialog::class_file_changed);

    if (!directory.empty()) {
        watcher_->start(path_);
    }

    std::thread([] {
        FastFileSearchDialog::find_files_async(FastFileSearchDialog::instance_);
    }).detach();
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
            return human_search(term.toStdString(), file.toStdString());
        });

        if (match) {
            filtered_files.append(file);
        }
    }

    display_files(filtered_files);
}


void FastFileSearchDialog::display_files(const QStringList &file_paths, bool resize)
{
    file_table_->setSortingEnabled(false);
    file_table_->setRowCount(0);

    for (int i = 0; i < file_paths.size(); ++i) {
        QFileInfo file_info(file_paths[i]);
        QString relative_path = QDir(path_).relativeFilePath(file_paths[i]); // Strip directory
        QString date_text = QLocale().toString(file_info.lastModified(), QLocale::ShortFormat);
        QString size_text = QString::fromStdString(human_size(file_info.size()));

        auto *path_item = new SortableTableWidgetItem(i, relative_path);
        auto *date_item = new QTableWidgetItem(date_text);
        auto *size_item = new SortableTableWidgetItem(i, size_text);

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

    // Resize columns only if reloading the full directory
    if (resize)
        file_table_->resizeColumnsToContents();

    file_table_->setSortingEnabled(true);
}



void FastFileSearchDialog::class_file_changed()
{
    std::cout << "class_file_changed" << std::endl;

    instance_->files_ = instance_->find_files();
    emit UpdateSignal::instance().filesUpdated();
}

void FastFileSearchDialog::instance_update_files()
{
    std::cout << "instance_update_files" << std::endl;
    display_files(files_, true);
}

void FastFileSearchDialog::update_files()
{
    files_ = find_files();
    display_files(files_, true);
    set_title();
}

void FastFileSearchDialog::on_select_directory()
{
    QString new_search_path = QFileDialog::getExistingDirectory(this, "Select Search Path", path_);
    if (!new_search_path.isEmpty()) {
        directory_changed(new_search_path.toStdString(), this);
    }
}

void FastFileSearchDialog::on_open_file_dialog()
{
    selected_items_.clear();
    selected_items_ = QFileDialog::getOpenFileNames(this, "Open Files", path_, "PDF Files (*.pdf)");
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
        QString file_path = path_ + "/" + selected_items[0]->text();
        QString directory_path = QFileInfo(file_path).absolutePath();
        on_open_file_dialog();
    }
}

void FastFileSearchDialog::on_item_double_click(QTableWidgetItem *item)
{
    selected_items_.clear();
    selected_items_.append(path_ + "/" + item->text());
    accept();
}

void FastFileSearchDialog::show_help()
{
    QDesktopServices::openUrl(QUrl(":/MusicReader/help/fast_search_help.html"));
}

std::pair<std::vector<std::string>, std::string> FastFileSearchDialog::selected_files() const
{
    std::vector<std::string> selected_paths;
    std::filesystem::path base_path = path_.toStdString();

    for (const QModelIndex &index : file_table_->selectionModel()->selectedRows()) {
        auto *item = file_table_->item(index.row(), 0); // Get file name from first column
        if (item) {
            std::filesystem::path full_path = base_path / item->text().toStdString();
            selected_paths.push_back(full_path.lexically_normal().string()); // Normalize the path
        }
    }

    return { selected_paths, base_path.string() };
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
    } else {
        QDialog::keyPressEvent(event);
    }
}

bool FastFileSearchDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::EnterWhatsThisMode) {
        show_help();
        return true;
    }
    return QDialog::eventFilter(object, event);
}


QStringList FastFileSearchDialog::find_files()
{
    QStringList file_paths;
    QDirIterator it(path_, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString file_path = it.next();
        if (file_ending_.isEmpty() || file_path.endsWith(file_ending_, Qt::CaseInsensitive)) {
            file_paths.append(file_path);
        }
    }
    return file_paths;
}

void FastFileSearchDialog::find_files_async(FastFileSearchDialog *instance)
{
    std::thread([instance]() {
        auto files = instance->find_files();  // Call on instance

        // Ensure UI update happens in the main thread
        QMetaObject::invokeMethod(instance, [instance, files = std::move(files)]() {
            instance->files_ = files;
            instance->display_files(instance->files_);
        }, Qt::QueuedConnection);
    }).detach();
}

void FastFileSearchDialog::directory_changed(const std::string &new_search_path, FastFileSearchDialog *self)
{
    if (new_search_path.empty()) return;

    path_ = QDir::toNativeSeparators(QString::fromStdString(new_search_path));

    if (watcher_) {
        watcher_->start(path_);
    }

    if (self) {
        self->update_files();
    }
}

void FastFileSearchDialog::set_title()
{
    if (!path_.isEmpty()) {
        setWindowTitle(QString("Fast File Search: %1").arg(path_));
    } else {
        setWindowTitle("Fast File Search");
    }
}
