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
#include <QtConcurrent/QtConcurrent>
#include <qpointer.h>

#include "bookmark_titlebar.h"
#include "bookmark_treewidget.h"
#include "document.h"
#include "logger.h"
#include "bookmark_panel.h"
#include "pdf_viewer.h"
#include "wait_cursor.h"
#include "exception_logger.h"
#include "status_bar.h"
#include "config_dialog.h"
#include "fast_file_search_dialog.h"
#include "qt_utils.h"
#include "utils.h"
#include "fullscreen_exit_button.h"
#include "vertical_tabs_widget.h"
#include "file_viewer.h"


MusicReader::MusicReader(QWidget *parent)
    : QMainWindow(parent)
{
    setup_UI();
}


void MusicReader::setup_UI()
{
    // this will search the directories and create the fast search dialog
    // asynchronously, because it can take many seconds to populate all the
    // files. Users of
    initialize_fast_search();

    this->resize(600, 400);
    this->setWindowTitle("MusicReader");

    splitter_ = new QSplitter(Qt::Horizontal, this);

    create_bookmark_panel();
    splitter_->addWidget(bookmark_panel_);

    tab_widget_ = config_.horiz_tabs() ? new VerticalTabsWidget : new QTabWidget;
    tab_widget_->setTabsClosable(true);
    tab_widget_->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(tab_widget_, &QTabWidget::tabCloseRequested, this, &MusicReader::on_close_tab);
    connect(tab_widget_, &QTabWidget::currentChanged, this, &MusicReader::on_tab_changed);
    connect(tab_widget_, &QTabWidget::customContextMenuRequested, this, &MusicReader::show_context_menu);


    connect(this, &MusicReader::document_loaded, this, [this](std::string name, int page) {
        auto i = doc_is_open(name);
        if (i.has_value()) {
            auto viewer = viewer_tab(i.value());
            if (viewer)
                viewer->get_page(page, true);  // Show the loaded page
        }
    });

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

    create_menus();
    create_toolbar();
    create_status_bar();

    setWindowIcon(QIcon(":/MusicReader/images/gclef.png"));

    restore_window_state();

    if (config_.restore_documents())
        restore_open_documents();

    // hides background image if there are open documents
    update_background();

    exit_button_ = new FullscreenExitButton(this);
    qApp->installEventFilter(this);

    QTimer *autosave_timer = new QTimer(this);
    connect(autosave_timer, &QTimer::timeout, this, [this]() {
        for (int i = 0; i < tab_widget_->count(); ++i) {
            PDFViewer *viewer = viewer_tab(i);
            if (viewer) {
                auto doc = viewer->document();
                if (doc) {
                    QtConcurrent::run([doc]() {
                        doc->save();
                    });
                }
            }
        }
    });
    autosave_timer->start(30'000);  // 30 seconds
}


bool MusicReader::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseMove && isFullScreen()) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        auto y = QCursor::pos().y();
        if (y <= 15) { // If mouse is near the top
            if (!exit_button_->isVisible())
                exit_button_->showAtTop();
            return true;
        } else if (y > 200 && exit_button_->isVisible()) {
            exit_button_->hideWithAnimation();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MusicReader::closeEvent(QCloseEvent *event)
{
    SAFE_METHOD;

    save_window_state_to_config();
    save_config();

    QMainWindow::closeEvent(event);  // Call base class implementation
    check_for_errors_on_exit();
}

void MusicReader::save_window_state_to_config()
{
    SAFE_METHOD;

    ConfigFileGroupSave group_saver(config_);

    // Save the currently open tab index
    config_.set_open_tab(tab_widget_->currentIndex());

    // Ensure position values are non-negative to prevent config errors
    int x = std::max(0, pos().x());
    int y = std::max(0, pos().y());
    int width = size().width();
    int height = size().height();

    config_.set_app_size({ x, y, width, height });

    // Save the toolbar location (Qt enum values match ToolbarLocation)
    config_.set_toolbar_location(static_cast<ToolbarLocation>(toolBarArea(toolbar_)));
}

void MusicReader::check_for_errors_on_exit()
{
    SAFE_METHOD;

    if (logged_error()) {
        QMessageBox msg_box(this);
        msg_box.setWindowTitle("Internal Errors");
        msg_box.setText("There were internal errors");
        msg_box.setIcon(QMessageBox::Warning);

        QPushButton *ok_button = msg_box.addButton("Ignore", QMessageBox::AcceptRole);
        QPushButton *view_errors_button = msg_box.addButton("View errors", QMessageBox::ActionRole);
        msg_box.setDefaultButton(view_errors_button);

        msg_box.exec();

        if (msg_box.clickedButton() == view_errors_button) {
            show_log_content();
        } else if (msg_box.clickedButton() == ok_button) {
            msg_box.close();
        }
    }
}

void MusicReader::show_log_content()
{
    SAFE_METHOD;

    QString log_content = QString::fromStdString(logger::get_log_content());

    QDialog dialog(this);
    dialog.setWindowTitle("Log Content");
    dialog.setModal(true);

    QVBoxLayout layout(&dialog);

    QScrollArea scroll_area(&dialog);
    scroll_area.setWidgetResizable(true);

    QTextEdit text_edit;
    text_edit.setText(log_content);
    text_edit.setReadOnly(true);

    scroll_area.setWidget(&text_edit);
    layout.addWidget(&scroll_area);

    QHBoxLayout button_layout;

    // Future enhancement: Report button (commented out)
    /*
    QPushButton report_button("Report Issue");
    QObject::connect(&report_button, &QPushButton::clicked, [&]() {
        report_issue(log_content.toStdString());
    });
    button_layout.addWidget(&report_button);
    */

    QPushButton ok_button("OK");
    QObject::connect(&ok_button, &QPushButton::clicked, &dialog, &QDialog::accept);

    button_layout.addStretch();
    button_layout.addWidget(&ok_button);

    layout.addLayout(&button_layout);

    dialog.setLayout(&layout);
    dialog.resize(800, 600);
    dialog.exec();
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

    QAction *exit_action = new QAction("E&xit", this);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);
    file_menu->addAction(exit_action);

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

    toolbar_menu_action_ = new QAction("&Tool Bar", this);
    toolbar_menu_action_->setShortcut(shortcuts_["toolbar"]);
    toolbar_menu_action_->setCheckable(true);
    toolbar_menu_action_->setChecked(true);
    connect(toolbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_toolbar_visibility);
    view_menu->addAction(toolbar_menu_action_);

    menubar_menu_action_ = new QAction("&Menu Bar", this);
    menubar_menu_action_->setCheckable(true);
    menubar_menu_action_->setChecked(config_.show_menu());
    connect(menubar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_menu_visibility);
    view_menu->addAction(menubar_menu_action_);

    statusbar_menu_action_ = new QAction("&Status Bar", this);
    statusbar_menu_action_->setCheckable(true);
    statusbar_menu_action_->setChecked(config_.show_status_bar());
    connect(statusbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_statusbar_visibility);
    view_menu->addAction(statusbar_menu_action_);

    /*action = new QAction("&Light Theme", this);
    action->setCheckable(true);
    action->setChecked(config_.theme() == Theme::Light);
    connect(action, &QAction::triggered, this, &MusicReader::set_light_theme);
    light_theme_menu_item_ = action;
    view_menu->addAction(action);

    action = new QAction("&Dark Theme", this);
    action->setCheckable(true);
    action->setChecked(config_.theme() == Theme::Dark);
    connect(action, &QAction::triggered, this, &MusicReader::set_dark_theme);
    dark_theme_menu_item_ = action;
    view_menu->addAction(action);*/

    action = new QAction("View Log...", this);
    connect(action, &QAction::triggered, this, &MusicReader::show_log_file);
    view_menu->addAction(action);

    update_undo_redo_state();

    menuBar()->setStyleSheet(R"(
        QMenu::item {
            font-weight: normal;
        }
        QMenu::item:disabled {
            color: gray;
        })");

    // Make global keyboard shortcuts within the app
    QShortcut *shortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::add_bookmark);

    shortcut = new QShortcut(QKeySequence("PageUp"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::on_page_up);

    shortcut = new QShortcut(QKeySequence("Ctrl+B"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::toggle_bookmark_panel);

    shortcut = new QShortcut(Qt::Key_Space, this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto viewer = current_viewer();
        if (viewer) viewer->page_down();
    });

    shortcut = new QShortcut(QKeySequence("F5"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::reload_document);

    shortcut = new QShortcut(QKeySequence("F2"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::edit_document);

    if (!config_.show_menu()) 
        menu_bar->hide();    
}

void MusicReader::update_recent_files_list()
{
    open_recent_menu_->clear();

    for (const auto &path : config_.recent_documents()) {
        QString display_text = QString::fromStdString(path.string());
        QAction *action = new QAction(display_text, this);
        action->setToolTip(QString::fromStdString(path.string()));

        connect(action, &QAction::triggered, this, [this, path]() {
            open_pdf_in_tab(path.string(), 1);
        });

        open_recent_menu_->addAction(action);
    }
}


void MusicReader::show_context_menu(const QPoint &pos)
{
    QMenu context_menu(this);

    QAction *reload_action = new QAction("Reload", this);
    reload_action->setShortcut(QKeySequence("F5"));
    connect(reload_action, &QAction::triggered, this, &MusicReader::reload_document);

    QAction *edit_action = new QAction("Edit...", this);
    edit_action->setShortcut(QKeySequence("F2"));
    connect(edit_action, &QAction::triggered, this, &MusicReader::edit_document);

    QAction *open_folder_action = new QAction("Open from containing folder...", this);
    connect(open_folder_action, &QAction::triggered, this, [this]() { open_file_dialog(); });

    QAction *browse_folder_action = new QAction("Browse containing folder...", this);
    connect(browse_folder_action, &QAction::triggered, this, &MusicReader::browse_folder);

    context_menu.addAction(reload_action);
    context_menu.addSeparator();
    context_menu.addAction(edit_action);
    context_menu.addAction(open_folder_action);
    context_menu.addAction(browse_folder_action);

    context_menu.exec(tab_widget_->mapToGlobal(pos));
}



void MusicReader::browse_folder()
{
    auto doc = current_document();
    if (!doc) return;

    auto file_path = QString::fromStdString(doc->filename());
    QFileInfo file_info(file_path);
    if (!file_info.exists()) return;

    QString folder_path = file_info.absolutePath();
    QProcess::startDetached("explorer", { folder_path });
}

void MusicReader::show_log_file()
{
    SAFE_METHOD;
    auto viewer = new FileViewer("C:\\Users\\rlabbe\\AppData\\Roaming\\MusicReader\\MusicReader.log", this);
    viewer->show();
}


bool MusicReader::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN

    if (!config_.show_menu()) {
        MSG *msg = static_cast<MSG *>(message);

        if (msg->message == WM_SYSCOMMAND && (msg->wParam & 0xFFF0) == SC_MOUSEMENU) {
            show_titlebar_menu();
            *result = 0;
            return true; // Prevents Windows from showing its own menu
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}


void MusicReader::reload_document()
{
    SAFE_METHOD;
    auto viewer = current_viewer();
    auto doc = current_document();
    if (!viewer || !doc) return;

    int page_num = viewer->current_page();

    open_pdf_in_tab(doc->filename(), page_num, viewer);
}

void MusicReader::edit_document()
{
    SAFE_METHOD;
    auto doc = current_document();
    if (!doc) return;

    auto filename = doc->filename();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(filename)));
}


void MusicReader::show_titlebar_menu()
{
    QMenu menu(this);

    QMenu *file_menu = menu.addMenu("&File");
    QAction *open_action = new QAction("&Open...", this);
    open_action->setShortcut(shortcuts_["open_file"]);
    connect(open_action, &QAction::triggered, this, [this]() { open_file_dialog(""); });

    file_menu->addAction(open_action);

    QAction *fast_search_action = new QAction("&Fast Search...", this);
    fast_search_action->setShortcut(shortcuts_["fast_search"]);
    connect(fast_search_action, &QAction::triggered, this, &MusicReader::open_fast_search_dialog);
    file_menu->addAction(fast_search_action);

    QAction *settings_action = new QAction("&Settings...", this);
    settings_action->setShortcut(shortcuts_["settings"]);
    connect(settings_action, &QAction::triggered, this, &MusicReader::open_config_dialog);
    file_menu->addAction(settings_action);

    open_recent_menu_ = new QMenu("Open &Recent", this);
    file_menu->addMenu(open_recent_menu_);
    connect(open_recent_menu_, &QMenu::aboutToShow, this, &MusicReader::update_recent_files_list);

    QAction *dpi_action = new QAction("Recompute DPI", this);
    connect(dpi_action, &QAction::triggered, this, [this]() { update_dpi_setting(true); });
    file_menu->addAction(dpi_action);

    QMenu *edit_menu = menu.addMenu("&Edit");

    edit_margin_action_ = new QAction("Edit Document Margin", this);
    edit_margin_action_->setCheckable(true);
    connect(edit_margin_action_, &QAction::triggered, this, &MusicReader::toggle_draw_margin);
    edit_menu->addAction(edit_margin_action_);

    undo_action_ = new QAction("Undo", this);
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, bookmark_panel_, &BookmarkPanel::undo);
    undo_action_->setDisabled(true);
    edit_menu->addAction(undo_action_);
    addAction(undo_action_);

    redo_action_ = new QAction("Redo", this);
    redo_action_->setShortcut(QKeySequence::Redo);
    connect(redo_action_, &QAction::triggered, bookmark_panel_, &BookmarkPanel::redo);
    redo_action_->setEnabled(false);
    edit_menu->addAction(redo_action_);
    addAction(redo_action_);

    QMenu *view_menu = menu.addMenu("&View");

    bookmark_menu_action_ = new QAction("Show Bookmarks", this);
    bookmark_menu_action_->setCheckable(true);
    bookmark_menu_action_->setChecked(true);
    connect(bookmark_menu_action_, &QAction::triggered, this, &MusicReader::toggle_bookmark_panel);
    view_menu->addAction(bookmark_menu_action_);

    QAction *toolbar_action = new QAction("&Tool Bar", this);
    toolbar_action->setShortcut(shortcuts_["toolbar"]);
    toolbar_action->setCheckable(true);
    toolbar_action->setChecked(true);
    connect(toolbar_action, &QAction::triggered, this, &MusicReader::toggle_toolbar_visibility);
    view_menu->addAction(toolbar_action);

    statusbar_menu_action_ = new QAction("&Status Bar", this);
    statusbar_menu_action_->setCheckable(true);
    statusbar_menu_action_->setChecked(config_.show_status_bar());
    connect(statusbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_statusbar_visibility);
    view_menu->addAction(statusbar_menu_action_);

    /*QAction *light_theme_action = new QAction("&Light Theme", this);
    light_theme_action->setCheckable(true);
    light_theme_action->setChecked(config_.theme() == Theme::Light);
    connect(light_theme_action, &QAction::triggered, this, &MusicReader::set_light_theme);
    light_theme_menu_item_ = light_theme_action;
    view_menu->addAction(light_theme_action);

    QAction *dark_theme_action = new QAction("&Dark Theme", this);
    dark_theme_action->setCheckable(true);
    dark_theme_action->setChecked(config_.theme() == Theme::Dark);
    connect(dark_theme_action, &QAction::triggered, this, &MusicReader::set_dark_theme);
    dark_theme_menu_item_ = dark_theme_action;
    view_menu->addAction(dark_theme_action);*/

    QAction *action = new QAction("View Log...", this);
    connect(action, &QAction::triggered, this, &MusicReader::show_log_file);
    view_menu->addAction(action);

    // Separator before Exit
    menu.addSeparator();

    // Standalone Exit Action (Below View)
    QAction *exit_action = new QAction("E&xit", this);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);
    menu.addAction(exit_action);

    menu.exec(QCursor::pos());
}



void MusicReader::add_bookmark()
{
    SAFE_METHOD;

    if (bookmark_panel_)
        bookmark_panel_->add_bookmark();
}


void MusicReader::create_toolbar()
{
    Qt::ToolBarArea toolbar_location = Qt::TopToolBarArea;

    toolbar_ = new QToolBar("Toolbar");
    Qt::ToolBarArea area;
    switch (config_.toolbar_location()) {
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

    if (in_single_page_mode()) {
        action = new QAction(single_icon_, "", this);
    } else {
        action = new QAction(double_icon_, "", this);
    }

    action->setToolTip("View one/two pages");
    connect(action, &QAction::triggered, this, &MusicReader::on_toggle_view_mode);
    toolbar_->addAction(action);
    view_toggle_action_ = action;

    zoomin_icon_ = QIcon(QPixmap(":/MusicReader/images/zoomin.svg"));
    zoomout_icon_ = QIcon(QPixmap(":/MusicReader/images/zoomout.svg"));

    action = new QAction(config_.zoom_to_content() ? zoomout_icon_ : zoomin_icon_, "", this);
    connect(action, &QAction::triggered, this, &MusicReader::toggle_page_zoom);
    action->setToolTip("Toggle zoom to content");
    toolbar_->addAction(action);
    zoom_in_out_action_ = action;

    action = new QAction(QIcon(QPixmap(":/MusicReader/images/gear.png")), "", this);
    action->setToolTip("Settings");
    connect(action, &QAction::triggered, this, &MusicReader::open_config_dialog);
    toolbar_->addAction(action);

    /* TODO
    action = new QAction(QIcon(QPixmap(":/MusicReader/images/border.svg")), "", this);
    action->setCheckable(true);
    action->setToolTip("Edit document margin");
    connect(action, &QAction::triggered, this, &MusicReader::toggle_draw_margin);
    toolbar_->addAction(action);
    margin_action_ = action;
    */

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
    ConfigFileGroupSave group_saver(config_);

    // Toggle between single and double page view mode
    config_.set_page_view_count((config_.page_view_count() == 1) ? 2 : 1);

    // Update the icon
    view_toggle_action_->setIcon(config_.page_view_count() == 1 ? single_icon_ : double_icon_);

    // Emit signal to notify viewers
    emit view_mode_signal_(config_.page_view_count());
}


bool MusicReader::in_single_page_mode() const
{
    return config_.page_view_count() == 1;
}

void MusicReader::toggle_page_zoom()
{
    // Toggle zoom setting and save config
    config_.set_zoom_to_content(!config_.zoom_to_content());
    config_.save();

    // Update the icon
    QIcon icon = config_.zoom_to_content() ? zoomout_icon_ : zoomin_icon_;
    zoom_in_out_action_->setIcon(icon);

    // Emit signal to notify viewers
    emit view_mode_signal_(config_.page_view_count());
}

PDFViewer *MusicReader::current_tab() const
{
    if (!tab_widget_) return nullptr;

    int i = tab_widget_->currentIndex();
    if (i >= 0) {
        QWidget *tab_widget = tab_widget_->widget(i);
        for (QObject *child : tab_widget->children()) {
            if (auto *viewer = qobject_cast<PDFViewer *>(child)) {
                return viewer;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<Document> MusicReader::current_document(const std::string &log_msg) const
{
    PDFViewer *tab = current_tab();
    if (tab) return tab->document();

    if (!log_msg.empty()) logger::log_error(log_msg);
    return nullptr;
}

std::pair<int, bool> MusicReader::current_page(const std::string &log_msg) const
{
    PDFViewer *viewer = current_viewer(log_msg);
    if (viewer) return { viewer->current_page(), true };

    if (!log_msg.empty()) logger::log_error(log_msg);
    return { 1, false };
}

std::string MusicReader::current_document_name() const
{
    auto doc = current_document();
    return doc ? doc->filename() : "";
}

std::shared_ptr<Document> MusicReader::document_at(int index) const
{
    PDFViewer *tab = viewer_tab(index);
    return tab ? tab->document() : nullptr;
}

PDFViewer *MusicReader::viewer_tab(int index) const
{
    try {
        QWidget *tab = tab_widget_->widget(index);
        for (QObject *child : tab->children()) {
            if (auto *viewer = qobject_cast<PDFViewer *>(child)) {
                return viewer;
            }
        }
    } catch (...) {
        return nullptr;
    }
    return nullptr;
}

void MusicReader::save_config()
{
    save_open_documents_to_config();
    logger::enable_debug_logging(config_.log_level() == LogLevel::Diagnostic);
}


void MusicReader::save_open_documents_to_config()
{
    SAFE_METHOD;

    std::vector<OpenDocument> open_documents;
    open_documents.reserve(tab_widget_->count());
    for (int index = 0; index < tab_widget_->count(); ++index) {
        auto pdf_viewer = tab_widget_->widget(index)->findChild<PDFViewer *>();
        if (pdf_viewer) {
            auto doc = pdf_viewer->document();
            auto name = doc->filename();
            open_documents.push_back({
                name, pdf_viewer->current_page(), doc->page_count()
            });
        }
    }
    config_.set_open_documents(open_documents);
}


void MusicReader::on_tab_changed()
{
    SAFE_METHOD;

    update_title();
    update_bookmark_panel();
    auto viewer = current_viewer();
    if (viewer) viewer->update_status_bar();
}


void MusicReader::on_close_tab(int index)
{
    SAFE_METHOD;

    auto doc = document_at(index);
    if (doc) {
        doc->kill_load();
        // This function is only called when explicitly closing a tab, not on app shutdown,
        // so it's safe to add the document to the recent documents list.
        config_.add_recent_document(doc->filename());
    }

    QWidget *widget_to_remove = tab_widget_->widget(index);
    if (widget_to_remove) {
        widget_to_remove->deleteLater();  // Perform cleanup
        tab_widget_->removeTab(index);
    }

    save_open_documents_to_config();
    on_tab_changed(); // this will update the UI for whatever tab is now current
}

void MusicReader::update_title(int index)
{
    SAFE_METHOD;
    index;

    if (tab_widget_->count() > 0) {
        QString current_tab_title = tab_widget_->tabText(tab_widget_->currentIndex());
        setWindowTitle(current_tab_title);
    } else {
        setWindowTitle("MusicReader");
    }
}
void MusicReader::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11) {
        if (isFullScreen()) {
            status_bar_->setVisible(config_.show_status_bar());
            config_.show_menu() ? menuBar()->show() : menuBar()->hide();
            showNormal();
        } else {
            menuBar()->hide();
            if (status_bar_) status_bar_->setVisible(false);
            showFullScreen();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (isFullScreen()) {
            status_bar_->setVisible(config_.show_status_bar());
            config_.show_menu() ? menuBar()->show() : menuBar()->hide();
            showNormal();
        }
        event->accept();

        return;
    }

    auto tab = current_viewer();
    if (!tab) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_PageUp: tab->page_up(); event->accept(); return;
    case Qt::Key_PageDown: tab->page_down(); event->accept(); return;
    case Qt::Key_Left: tab->change_page(-1); event->accept(); return;
    case Qt::Key_Right: tab->change_page(1); event->accept(); return;
    default: QMainWindow::keyPressEvent(event);
    }
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
    try {
        PDFViewer *tab = current_tab();
        if (tab) return tab;

        if (!log_err.empty()) logger::log_error(log_err);
    } catch (...) {
        if (!log_err.empty()) logger::log_error(log_err);
    }
    return nullptr;
}


void MusicReader::create_bookmark_panel()
{
    bookmark_panel_ = new BookmarkPanel(this);
    [[maybe_unused]] bool s = connect(bookmark_panel_, &BookmarkPanel::bookmark_clicked, this, &MusicReader::go_to_bookmark);

    connect(bookmark_panel_, &BookmarkPanel::bookmark_visibility_changed, this, &MusicReader::update_menu_bookmark_visibility);
}


void MusicReader::update_bookmark_panel()
{
    SAFE_METHOD;

    bookmark_panel_->populate();
    update_background();
}


void MusicReader::update_bookmarks_for_doc()
{
    bookmark_panel_->populate();
    update_background();
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
    for (int i = 0; i < tab_widget_->count(); ++i) {
        auto widget = viewer_tab(i);
        if (widget && widget->document()->filename() == name) return i;
    }

    return std::nullopt;
}


void MusicReader::go_to_bookmark(int page_num)
{
    SAFE_METHOD;

    if (page_num > 0) {
        auto *viewer = current_viewer();
        if (viewer)
            viewer->get_page(page_num);
        else
            logger::log_error("No viewer to navigate to bookmark");
    }

    // Updates Undo and Redo menu states based on stack availability
    /*TODO
    bool enable_undo = !undo_stack_.empty();
    bool enable_redo = !redo_stack_.empty();
    undo_action_->setEnabled(enable_undo);
    redo_action_->setEnabled(enable_redo);*/
}


void MusicReader::update_menu_bookmark_visibility()
{
    //TODO
}


void MusicReader::refresh_all_documents()
{
    for (int index = 0; index < tab_widget_->count(); ++index) {
        PDFViewer *viewer = viewer_tab(index);
        if (viewer) {
            viewer->refresh();
        }
    }
}


void MusicReader::open_config_dialog()
{
    try {
        ConfigDialog editor_dialog(config_, this);
        save_open_documents_to_config();

        editor_dialog.exec();

        if (editor_dialog.result() == QDialog::Accepted) {
            config_.save();
            on_config_saved();

            logger::enable_debug_logging(config_.log_level() == LogLevel::Diagnostic);
        }
    } catch (const std::exception &e) {
        logger::log_error("Failed to open settings dialog: " + std::string(e.what()));
    }
}


void MusicReader::restore_window_state()
{
    if (config_.restore_window_position()) {
        try {
            const auto &app_size = config_.app_size();
            if (app_size.size() >= 4) {
                move(app_size[0], app_size[1]);
                resize(app_size[2], app_size[3]);
                ensure_window_is_visible(this);
            }
        } catch (...) {
            logger::log_error("Failed to restore app position and size");
        }
    }
}

void MusicReader::open_file_dialog(const std::string &pathname)
{
    std::string default_directory;

    if (!pathname.empty()) {
        default_directory = pathname;
    } else {
        auto doc = current_document();
        if (doc)
            default_directory = std::filesystem::path(doc->filename()).parent_path().string();
    }

    QStringList filenames = QFileDialog::getOpenFileNames(
        this, "Open PDF", QString::fromStdString(default_directory), "PDF Files (*.pdf)");

    if (!filenames.isEmpty()) {
        WaitCursor cursor;
        for (const QString &name : filenames)
            open_pdf_in_tab(name.toStdString());
    }
}

PDFViewer *MusicReader::open_pdf_in_tab(const std::string &filename, int page, PDFViewer *viewer)
{
    SAFE_METHOD;

    // viewer will be nonnull if reloading document from F5

    if (!viewer) {
        if (auto i = doc_is_open(filename); i.has_value()) {
            focus_on_tab(i.value());
            return nullptr;
        }
    }

    WaitCursor cursor;

    auto doc = open_pdf_document(filename, page);
    if (!doc)
        return nullptr;

    connect(doc.get(), &Document::bookmarks_loaded, this, [&]() {
        bookmark_panel_->populate();
        update_background();
    });

    logger::log_info("Opened " + doc->filename());

    if (!viewer) {
        QWidget *tab = new QWidget();
        viewer = new PDFViewer(doc, &config_, page, status_bar_, tab);

        QVBoxLayout *layout = new QVBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(viewer);
        tab->setLayout(layout);

        int index = tab_widget_->addTab(tab, QString::fromStdString(std::filesystem::path(filename).stem().string()));
        tab_widget_->setTabToolTip(index, QString::fromStdString(filename));
        tab_widget_->setCurrentWidget(tab);

        focus_on_tab(tab_widget_->currentIndex());
        connect(this, &MusicReader::view_mode_signal_, viewer, &PDFViewer::refresh);
    } else
        viewer->replace_document(doc, page);

    save_open_documents_to_config();

    std::thread([this, doc, page]() {
        doc->load_document();  // Load pages asynchronously
    }).detach();

    viewer->refresh();
    return viewer;
}


std::shared_ptr<Document> MusicReader::open_pdf_document(const std::string &filename, int page_num)
{
    LOG_EXCEPTION;

    if (!std::filesystem::exists(filename)) {
        logger::log_error(filename + " doesn't exist");
        return {};
    }

    return std::make_shared<Document>(filename, config_.dpi(), page_num);
}


void MusicReader::focus_on_tab(int index)
{
    SAFE_METHOD;

    PDFViewer *viewer = viewer_tab(index);
    if (viewer) {
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
    status_bar_->setVisible(config_.show_status_bar());

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

    auto format_memory = [](size_t bytes) -> std::string {
        static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_index < 4) {
            size /= 1024.0;
            unit_index++;
        }

        return std::to_string(static_cast<int>(size)) + " " + units[unit_index];
    };

    PROCESS_MEMORY_COUNTERS mem_info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem_info, sizeof(mem_info))) {
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
    if (!viewer) {
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
    if (viewer) {
        viewer->get_page(index + 1);  // Convert index to 1-based page number
    }
    show_page_count();  // Ensure status bar reflects any adjustments
}

void MusicReader::open_fast_search_dialog()
{
    if (!fast_search_dialog_) {
        WaitCursor cursor;

        auto dsize = config_.fast_search_dialog_size();
        QRect size(dsize[0], dsize[1], dsize[2], dsize[3]);

        fast_search_dialog_ = new FastFileSearchDialog(this, config_.music_directory().string(), size);
    }

    try {
        tab_widget_->setEnabled(false);
        fast_search_dialog_->show_dialog();
        //fast_search_dialog_->exec();
    } catch (const std::exception &e) {
        logger::log_error("Failed to open fast search dialog: " + std::string(e.what()));
    }

    tab_widget_->setEnabled(true);

    // Save dialog geometry
    QRect geometry = fast_search_dialog_->geometry();
    std::vector<int> size = { geometry.x(), geometry.y(), geometry.width(), geometry.height() };

    {
        ConfigFileGroupSave group_saver(config_);
        config_.set_fast_search_dialog_size(size);
        config_.set_music_directory(fast_search_dialog_->path());
    }

    // Handle selected files
    auto [selected_files, filepath] = fast_search_dialog_->selected_files();
    if (selected_files == std::vector<std::string>{"open"}) {
        open_file_dialog(filepath);
    } else {
        for (const auto &file : selected_files) {
            open_pdf_in_tab(file);
        }
    }
}

void MusicReader::update_background()
{
    if (tab_widget_->count() == 0) {
        tab_widget_->setStyleSheet(R"(
            background-image: url(":/MusicReader/images/gclef.png");
            background-position: center;
            background-repeat: no-repeat;
            background-attachment: fixed;
        )");
    } else {
        // Remove the background image only, without wiping other styles
        tab_widget_->setStyleSheet(R"(
            background: none;
        )");
    }
}


void MusicReader::reopen_all_documents()
{
    SAFE_METHOD;

    while (tab_widget_->count() > 0)
        // remove from back so qt doesn't spend time reindexing the tabs
        tab_widget_->removeTab(tab_widget_->count() - 1);

    restore_open_documents();
}



void MusicReader::restore_open_documents()
{
    // make a copy, as we open tabs it modifies open_documents
    const auto docs = config_.open_documents();
    int num_docs = static_cast<int>(docs.size());

    if (num_docs == 0) {
        update_background();
        return;
    }

    for (const auto &doc : docs) {
        open_pdf_in_tab(doc.u8filename(), doc.page);
    }

    // Ensure the last open tab is focused
    if (config_.open_tab() > -1) {
        if (config_.open_tab() < tab_widget_->count())
            focus_on_tab(config_.open_tab());
        else
            config_.set_open_tab(-1);
    } else {
        focus_on_tab(0);
        config_.set_open_tab(0);
    }

    if (tab_widget_->count() > 0)
        auto *first_viewer = viewer_tab(tab_widget_->currentIndex());

    bookmark_panel_->adjust_width();
    update_background();
}


void MusicReader::initialize_fast_search()
{
    FastFileSearchDialog::initialize_data(config_.music_directory().string());
}


void MusicReader::toggle_bookmark_panel()
{
    if (bookmark_panel_) {
        bool visible = !bookmark_panel_->isVisible();
        bookmark_panel_->setVisible(visible);
        bookmark_menu_action_->setChecked(visible);
    }
}


void MusicReader::set_statusbar_visibility()
{
    if (!status_bar_) return;
    if (!statusbar_menu_action_) return;

    bool visible = config_.show_status_bar();
    status_bar_->setVisible(visible);
    statusbar_menu_action_->setChecked(visible);
}


void MusicReader::toggle_statusbar_visibility()
{
    config_.set_show_status_bar(!config_.show_status_bar());
    set_statusbar_visibility();
}


void MusicReader::set_toolbar_visibility()
{
    if (!toolbar_) return;
    if (!toolbar_menu_action_) return;

    bool visible = config_.show_toolbar();
    config_.set_show_toolbar(visible);
    toolbar_->setVisible(visible);
    toolbar_menu_action_->setChecked(visible);
}


void MusicReader::toggle_toolbar_visibility()
{
    config_.set_show_toolbar(!config_.show_toolbar());
    set_toolbar_visibility();
}

void MusicReader::toggle_menu_visibility()
{
    config_.set_show_menu(!config_.show_menu());
    set_menu_visibility();
}

void MusicReader::set_menu_visibility()
{
    config_.show_menu() ? menuBar()->show() : menuBar()->hide();
}


void MusicReader::on_config_saved()
{
    SAFE_METHOD;

    set_toolbar_visibility();
    refresh_all_documents();
    set_statusbar_visibility();
    set_menu_visibility();
}