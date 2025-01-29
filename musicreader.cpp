#include "musicreader.h"
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#define NOMINMAX 
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <QtWidgets>
#include <QMenuBar>
#include <QKeySequence>
#include <QAction>

#include "bookmark_titlebar.h"
#include "bookmark_treewidget.h"
#include "document.h"
#include "logger.h"
#include "bookmark_panel.h"
#include "pdf_viewer.h"
#include "wait_cursor.h"
#include "exception_logger.h"
#include "status_bar.h"


void ensure_window_is_visible(QWidget *win, bool move_only = false)
{
    try
    {
        if (!move_only)
        {
            win->setVisible(true);
        }

        QRect screen_geometry = QGuiApplication::primaryScreen()->geometry();
        QRect window_geometry = win->geometry();

        if (!screen_geometry.contains(window_geometry.topLeft()))
        {
            win->move(screen_geometry.topLeft() + QPoint(50, 50));
        }
    }
    catch (const std::exception &e)
    {
        logger::log_error(std::string("ensure_window_is_visible: ") + e.what());
    }
}



MusicReader::MusicReader(QWidget *parent)
    : QMainWindow(parent)
{
    setup_UI();
}


void MusicReader::setup_UI()
{
    this->resize(600, 400);
    this->setWindowTitle("MusicReader");

    create_menus();
    create_toolbar();
    create_status_bar();

    splitter_ = new QSplitter(Qt::Horizontal, this);

    bookmark_panel_ = new BookmarkPanel(this);
    splitter_->addWidget(bookmark_panel_);

    tab_widget_ = new QTabWidget();
    tab_widget_->setTabsClosable(true);
    splitter_->addWidget(tab_widget_);
    splitter_->setStretchFactor(0, 0);  // Give bookmark panel minimal space
    splitter_->setStretchFactor(1, 1);  // Document view gets priority


    // Adjust the bookmark panel width to fit contents
    bookmark_panel_->adjust_width();

    // Determine content width, ensuring a reasonable minimum
    int content_width = std::max(bookmark_panel_->width() + 20, 100);

    // Set initial splitter sizes: Bookmark panel gets calculated width, rest goes to document view
    splitter_->setSizes({ content_width, width() - content_width });


    setCentralWidget(splitter_);



    setWindowIcon(QIcon(":/MusicReader/images/gclef.png"));

    restore_window_state();
}


void MusicReader::create_menus()
{
    QMenuBar *menu_bar = menuBar();

    // File menu
    QMenu *file_menu = menu_bar->addMenu("&File");

    QAction *action = new QAction("&Open...", this);
    action->setShortcut(shortcuts_["open_file"]);
    connect(action, &QAction::triggered, this, [this]() { open_file_dialog(); });

    file_menu->addAction(action);

    action = new QAction("&Fast Search...", this);
    action->setShortcut(shortcuts_["fast_search"]);
    connect(action, &QAction::triggered, this, &MusicReader::open_fast_search_dialog);
    file_menu->addAction(action);

    action = new QAction("&Settings...", this);
    action->setShortcut(shortcuts_["settings"]);
    connect(action, &QAction::triggered, this, &MusicReader::open_config_dialog);
    file_menu->addAction(action);

    open_recent_menu_ = new QMenu("Open &Recent", this);
    file_menu->addMenu(open_recent_menu_);
    connect(open_recent_menu_, &QMenu::aboutToShow, this, &MusicReader::update_recent_files_list);

    QAction *dpi_action = new QAction("Recompute DPI", this);
    connect(dpi_action, &QAction::triggered, this, [this]() { update_dpi_setting(true); });
    file_menu->addAction(dpi_action);

    // Edit menu
    edit_menu_ = menu_bar->addMenu("&Edit");

    edit_margin_action_ = new QAction("Edit Document Margin", this);
    edit_margin_action_->setCheckable(true);
    connect(edit_margin_action_, &QAction::triggered, this, &MusicReader::toggle_draw_margin);
    edit_menu_->addAction(edit_margin_action_);

    undo_action_ = new QAction("Undo", this);
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, bookmark_panel_, &BookmarkPanel::undo);
    undo_action_->setDisabled(true);
    edit_menu_->addAction(undo_action_);
    addAction(undo_action_);

    redo_action_ = new QAction("Redo", this);
    redo_action_->setShortcut(QKeySequence::Redo);
    connect(redo_action_, &QAction::triggered, bookmark_panel_, &BookmarkPanel::redo);
    redo_action_->setEnabled(false);
    edit_menu_->addAction(redo_action_);
    addAction(redo_action_);

    // View menu
    QMenu *view_menu = menu_bar->addMenu("&View");

    bookmark_menu_action_ = new QAction("Show Bookmarks", this);
    bookmark_menu_action_->setCheckable(true);
    bookmark_menu_action_->setChecked(true);
    connect(bookmark_menu_action_, &QAction::triggered, this, &MusicReader::toggle_bookmark_panel);
    view_menu->addAction(bookmark_menu_action_);

    action = new QAction("&Toolbar", this);
    action->setShortcut(shortcuts_["toolbar"]);
    action->setCheckable(true);
    action->setChecked(true);
    connect(action, &QAction::triggered, this, &MusicReader::toggle_toolbar_visibility);
    view_menu->addAction(action);

    action = new QAction("&Statusbar", this);
    action->setCheckable(true);
    action->setChecked(true);
    connect(action, &QAction::triggered, this, &MusicReader::toggle_statusbar_visibility);
    view_menu->addAction(action);

    action = new QAction("&Light Theme", this);
    action->setCheckable(true);
    action->setChecked(config_.theme == Theme::Light);
    connect(action, &QAction::triggered, this, &MusicReader::set_light_theme);
    light_theme_menu_item_ = action;
    view_menu->addAction(action);

    action = new QAction("&Dark Theme", this);
    action->setCheckable(true);
    action->setChecked(config_.theme == Theme::Dark);
    connect(action, &QAction::triggered, this, &MusicReader::set_dark_theme);
    dark_theme_menu_item_ = action;
    view_menu->addAction(action);

    update_undo_redo_state();

    menuBar()->setStyleSheet(R"(
        QMenu::item {
            font-weight: normal;
        }
        QMenu::item:disabled {
            color: gray;
        }
    )");
}


void MusicReader::create_toolbar()
{
    Qt::ToolBarArea toolbar_location = Qt::TopToolBarArea;

    toolbar_ = new QToolBar("Toolbar");
    Qt::ToolBarArea area;
    switch (config_.toolbar_location)
    {
    case ToolbarLocation::Top: area = Qt::TopToolBarArea; break;
    case ToolbarLocation::Bottom: area = Qt::BottomToolBarArea; break;
    case ToolbarLocation::Left: area = Qt::LeftToolBarArea; break;
    case ToolbarLocation::Right: area = Qt::RightToolBarArea; break;
    }
    addToolBar(area, toolbar_);

    QAction *action = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), "", this);
    action->setToolTip("Search for file...");
    connect(action, &QAction::triggered, this, &MusicReader::open_fast_search_dialog);
    toolbar_->addAction(action);

    action = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "", this);
    action->setToolTip("Open File...");
    connect(action, &QAction::triggered, this, [this]() { open_file_dialog(); });

    toolbar_->addAction(action);

    single_icon_ = style()->standardIcon(QStyle::SP_FileIcon);
    double_icon_ = create_double_icon();

    if (in_single_page_mode())
    {
        action = new QAction(single_icon_, "", this);
    }
    else
    {
        action = new QAction(double_icon_, "", this);
    }

    action->setToolTip("View one/two pages");
    connect(action, &QAction::triggered, this, &MusicReader::on_toggle_view_mode);
    toolbar_->addAction(action);
    view_toggle_action_ = action;

    zoomin_icon_ = QIcon(QPixmap(":/MusicReader/images/zoomin.svg"));
    zoomout_icon_ = QIcon(QPixmap(":/MusicReader/images/zoomout.svg"));

    action = new QAction(config_.zoom_to_content ? zoomout_icon_ : zoomin_icon_, "", this);
    connect(action, &QAction::triggered, this, &MusicReader::toggle_page_zoom);
    action->setToolTip("Toggle zoom to content");
    toolbar_->addAction(action);
    zoom_in_out_action_ = action;

    action = new QAction(QIcon(QPixmap(":/MusicReader/images/gear.png")), "", this);
    action->setToolTip("Settings");
    connect(action, &QAction::triggered, this, &MusicReader::open_config_dialog);
    toolbar_->addAction(action);

    action = new QAction(QIcon(QPixmap(":/MusicReader/images/border.svg")), "", this);
    action->setCheckable(true);
    action->setToolTip("Edit document margin");
    connect(action, &QAction::triggered, this, &MusicReader::toggle_draw_margin);
    toolbar_->addAction(action);
    margin_action_ = action;

    action = new QAction(QIcon(":/MusicReader/images/left.ico"), "PgUp", this);
    action->setToolTip("Previous page");
    connect(action, &QAction::triggered, this, &MusicReader::on_page_up);
    toolbar_->addAction(action);

    action = new QAction(QIcon(":/MusicReader/images/right.ico"), "PgDn", this);
    action->setToolTip("Next page");
    connect(action, &QAction::triggered, this, &MusicReader::on_page_down);
    toolbar_->addAction(action);
}


void MusicReader::on_toggle_view_mode()
{
    // Toggle between single and double page view mode
    config_.page_view_count = (config_.page_view_count == 1) ? 2 : 1;

    // Update the icon
    view_toggle_action_->setIcon(config_.page_view_count == 1 ? single_icon_ : double_icon_);

    // Emit signal to notify viewers
    emit view_mode_signal_(config_.page_view_count);
}


bool MusicReader::in_single_page_mode() const
{
    return config_.page_view_count == 1;
}

void MusicReader::toggle_page_zoom()
{
    // Toggle zoom setting and save config
    config_.zoom_to_content = !config_.zoom_to_content;
    config_.save();

    // Update the icon
    QIcon icon = config_.zoom_to_content ? zoomout_icon_ : zoomin_icon_;
    zoom_in_out_action_->setIcon(icon);

    // Emit signal to notify viewers
    emit view_mode_signal_(config_.page_view_count);
}

PDFViewer *MusicReader::current_tab() const
{
    if (!tab_widget_) return nullptr;

    int i = tab_widget_->currentIndex();
    if (i >= 0)
    {
        QWidget *tab_widget = tab_widget_->widget(i);
        for (QObject *child : tab_widget->children())
        {
            if (auto *viewer = qobject_cast<PDFViewer *>(child))
            {
                return viewer;
            }
        }
    }
    return nullptr;
}

Document *MusicReader::current_document(const std::string &log_msg) const
{
    PDFViewer *tab = current_tab();
    if (tab) return tab->document();

    if (!log_msg.empty()) logger::log_error(log_msg);
    return nullptr;
}

std::pair<int, bool> MusicReader::current_page(const std::string &log_msg) const
{
    PDFViewer *viewer = current_viewer(log_msg);
    if (viewer) return { viewer->current_page(), true};

    if (!log_msg.empty()) logger::log_error(log_msg);
    return {1, false};
}

std::string MusicReader::current_document_name() const
{
    Document *doc = current_document();
    return doc ? doc->filename() : "";
}

Document *MusicReader::document_at(int index) const
{
    PDFViewer *tab = viewer_tab(index);
    return tab ? tab->document() : nullptr;
}

PDFViewer *MusicReader::viewer_tab(int index) const
{
    try
    {
        QWidget *tab = tab_widget_->widget(index);
        for (QObject *child : tab->children())
        {
            if (auto *viewer = qobject_cast<PDFViewer *>(child))
            {
                return viewer;
            }
        }
    }
    catch (...)
    {
        return nullptr;
    }
    return nullptr;
}

void MusicReader::on_page_up()
{
    auto tab = current_viewer();
    if (tab) tab->page_up();
}

void MusicReader::on_page_down()
{
    auto tab = current_viewer();
    if (tab) tab->page_down();
}

PDFViewer *MusicReader::current_viewer(const std::string &log_err) const
{
    // TODO may not be correct. not sure there is some weird logic in the python
    // to detect the type of the object, may just have been do to earlier code that
    // no longer exists. 
    try
    {       
        PDFViewer *tab = current_tab();
        if (tab) return tab;

        if (!log_err.empty()) logger::log_error(log_err);
    }
    catch (...)
    {
        if (!log_err.empty()) logger::log_error(log_err);
    }
    return nullptr;
}


void MusicReader::create_bookmark_panel()
{
    bookmark_panel_ = new BookmarkPanel(this);
    bookmark_panel_ = new BookmarkPanel(this);
    connect(bookmark_panel_, &BookmarkPanel::bookmark_clicked, this, &MusicReader::go_to_bookmark);
    connect(bookmark_panel_, &BookmarkPanel::bookmark_visibility_changed, this, &MusicReader::update_menu_bookmark_visibility);

}


QIcon MusicReader::create_double_icon()
{
    QIcon single_icon = style()->standardIcon(QStyle::SP_FileIcon);
    QSize icon_size = single_icon.actualSize(QSize(32, 32));

    QPixmap pixmap(icon_size.width() * 2, icon_size.height());
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    single_icon.paint(&painter, 0, 0, icon_size.width(), icon_size.height());
    single_icon.paint(&painter, icon_size.width(), 0, icon_size.width(), icon_size.height());
    painter.end();

    return QIcon(pixmap);
}

std::optional<int> MusicReader::doc_is_open(std::filesystem::path name)
{
    for (int i = 0; i < tab_widget_->count(); ++i)
    {
        // TODO name might not be unique
        if (tab_widget_->tabText(i).toStdString() == name.filename().string())
        {
            return i;
        }
    }

    return std::nullopt;
}


void MusicReader::go_to_bookmark(int page_num)
{}

void MusicReader::update_menu_bookmark_visibility()
{
    //TODO
}

#include "config_file_dialog.h"

void MusicReader::save_open_documents_to_config()
{
    //TODO
}

void MusicReader::refresh_all_documents()
{
    for (int index = 0; index < tab_widget_->count(); ++index)
    {
        PDFViewer *viewer = viewer_tab(index);
        if (viewer)
        {
            viewer->refresh();
        }
    }
}


void MusicReader::open_config_dialog()
{
    try
    {
        ConfigDialog editor_dialog(config_, this);
        save_open_documents_to_config();

        editor_dialog.exec();

        if (editor_dialog.result() == QDialog::Accepted)
        {
            config_.save();
            refresh_all_documents();
            logger::enable_debug_logging(config_.log_level == LogLevel::Diagnostic);
        }
    }
    catch (const std::exception &e)
    {
        logger::log_error("Failed to open settings dialog: " + std::string(e.what()));
    }
}

void MusicReader::restore_window_state()
{
    if (config_.restore_window_position)
    {
        try
        {
            const auto &app_size = config_.app_size;
            if (app_size.size() >= 4)
            {
                move(app_size[0], app_size[1]);
                resize(app_size[2], app_size[3]);
                ensure_window_is_visible(this);
            }
        }
        catch (...)
        {
            logger::log_error("Failed to restore app position and size");
        }
    }
}

void MusicReader::open_file_dialog(const std::string &pathname)
{
    Document *doc = current_document();
    std::string default_directory;

    if (pathname.empty())
    {
        if (doc)
        {
            default_directory = std::filesystem::path(doc->filename()).parent_path().string();
        }
        else
        {
            default_directory = "";
        }
    }
    else
    {
        default_directory = pathname;
    }

    QStringList filenames = QFileDialog::getOpenFileNames(
        this, "Open PDF", QString::fromStdString(default_directory), "PDF Files (*.pdf)");

    if (!filenames.isEmpty())
    {
        WaitCursor cursor;
        for (const QString &name : filenames)
        {
            open_pdf_in_tab(name.toStdString());
        }
    }
}

void MusicReader::open_pdf_in_tab(const std::string &filename, int page)
{
    SAFE_METHOD;
    if (auto i = doc_is_open(filename); i.has_value())
    {
        focus_on_tab(i.value());
        return;
    }

    WaitCursor cursor;  // RAII-based wait cursor

    Document *doc = open_pdf_document(filename);
    if (!doc)
    {
        display_error_message("Can't open " + filename + ", is it a PDF?");
        return;
    }

    QWidget *tab = new QWidget();
    PDFViewer *viewer = new PDFViewer(doc, &config_, page, status_bar_, tab);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(viewer);
    tab->setLayout(layout);

    int index = tab_widget_->addTab(tab, QString::fromStdString(std::filesystem::path(filename).stem().string()));
    tab_widget_->setTabToolTip(index, QString::fromStdString(filename));
    tab_widget_->setCurrentWidget(tab);

    focus_on_tab(tab_widget_->currentIndex());
    connect(this, &MusicReader::view_mode_signal_, viewer, &PDFViewer::refresh);

    save_open_documents_to_config();
}


Document *MusicReader::open_pdf_document(const std::string &filename)
{
    LOG_EXCEPTION;

    if (!std::filesystem::exists(filename))
    {
        logger::log_error(filename + " doesn't exist");
        return nullptr;
    }

    return new Document(filename, config_.dpi);
}


void MusicReader::focus_on_tab(int index)
{
    SAFE_METHOD;

    PDFViewer *viewer = viewer_tab(index);
    if (viewer)
    {
        tab_widget_->setCurrentIndex(index);
        viewer->setFocusPolicy(Qt::StrongFocus);
        viewer->setFocus();
    }
}


void MusicReader::display_error_message(const std::string &msg)
{
    SAFE_METHOD;

    QMessageBox mbox;
    mbox.setIcon(QMessageBox::Critical);
    mbox.setText(QString::fromStdString(msg));
    mbox.setWindowTitle("Error");
    mbox.setStandardButtons(QMessageBox::Ok);
    mbox.exec();
}

bool MusicReader::display_query(const std::string &msg)
{
    SAFE_METHOD;

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(QString::fromStdString(msg));
    msgBox.setWindowTitle("Confirmation");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    return msgBox.exec() == QMessageBox::Yes;
}


void MusicReader::create_status_bar()
{
    status_bar_ = new StatusBar();
    setStatusBar(status_bar_);

    // Connect dropdown selection to page change
    connect(status_bar_->page_combo_box_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MusicReader::on_page_selected);

    // Display initial memory usage
    update_memory_usage();

    // Start timer to update memory usage every 5 seconds
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MusicReader::update_memory_usage);
    timer_->start(5000);
}

void MusicReader::update_memory_usage()
{
    SAFE_METHOD;

    auto format_memory = [](size_t bytes) -> std::string
    {
        static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_index < 4)
        {
            size /= 1024.0;
            unit_index++;
        }

        return std::to_string(static_cast<int>(size)) + " " + units[unit_index];
    };

    PROCESS_MEMORY_COUNTERS mem_info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem_info, sizeof(mem_info)))
    {
        size_t usage = mem_info.WorkingSetSize;
        MEMORYSTATUSEX mem_status;
        mem_status.dwLength = sizeof(mem_status);
        GlobalMemoryStatusEx(&mem_status);

        int pct = static_cast<int>(100.0 * usage / mem_status.ullTotalPhys);
        std::string msg = "Mem: " + format_memory(usage) + " (" + std::to_string(pct) + "%)";
        status_bar_->set_memory_usage(msg);
    }
}


void MusicReader::show_page_count()
{
    PDFViewer *viewer = current_viewer();
    if (!viewer)
    {
        status_bar_->clear_page_count();
        return;
    }

    int total_pages = viewer->page_count();
    int current_page = viewer->current_page();
    status_bar_->set_page_count(current_page, total_pages);
}

void MusicReader::on_page_selected(int index)
{
    PDFViewer *viewer = current_viewer();
    if (viewer)
    {
        viewer->get_page(index + 1);  // Convert index to 1-based page number
    }
    show_page_count();  // Ensure status bar reflects any adjustments
}


