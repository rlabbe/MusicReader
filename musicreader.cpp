#include "logger.h"
#include "musicreader.h"

#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <QtWidgets>
#include <QtConcurrent/QtConcurrent>

#include "bookmark_titlebar.h"
#include "bookmark_treewidget.h"
#include "document.h"
#include "bookmark_panel.h"
#include "pdf_viewer.h"
#include "performance_mode.h"
#include "exception_logger.h"
#include "status_bar.h"
#include "config_dialog.h"
#include "fast_file_search_dialog.h"
#include "qt_utils.h"
#include "utils.h"
#include "fullscreen_exit_button.h"
#include "vertical_tabs_widget.h"
#include "horizontal_tabs_widget.h"
#include "file_viewer.h"
#include "requires.h"
#include "imslp_search_dialog.h"
#include "dev_status_dialog.h"
#include "tour_dialog.h"
#include "wait_cursor.h"


constexpr int HIDE_MOUSE_TIMEOUT_MS = 5000;


MusicReader::MusicReader(QWidget* parent)
    : QMainWindow(parent)
    , load_manager_(std::min(80, (int)std::thread::hardware_concurrency()))
{
#if !defined(NDEBUG)
    logger::initialize(true, config_, 1024);
#else
    logger::initialize(true, config_, 1024);
#endif

    update_logging_level();
    setup_UI();
}


void MusicReader::setup_UI()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    this->resize(600, 400);
    this->setWindowTitle("MusicReader");

    splitter_ = new QSplitter(Qt::Horizontal, this);

    create_bookmark_panel();
    splitter_->addWidget(bookmark_panel_);

    if (config_.horiz_tabs())
        tab_widget_ = new VerticalTabsWidget;
    else
        tab_widget_ = new HorizontalTabWidget;

    tab_widget_->setTabsClosable(true);
    tab_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    tab_widget_->tabBar()->setFocusPolicy(Qt::NoFocus);
    tab_widget_->setUsesScrollButtons(false);

    connect(tab_widget_, &QTabWidget::tabCloseRequested, this, &MusicReader::on_close_tab);
    connect(tab_widget_, &QTabWidget::currentChanged, this, &MusicReader::on_tab_changed);
    connect(tab_widget_, &QTabWidget::customContextMenuRequested, this, &MusicReader::show_context_menu);

    // repaint the tab bar when a tab is moved, plus save the new order to config
    auto* tabBar = tab_widget_->tabBar();
    connect(tabBar, &QTabBar::tabMoved, this, &MusicReader::on_tab_moved);
    connect(this, &MusicReader::document_loaded, this, &MusicReader::on_document_loaded);

    splitter_->addWidget(tab_widget_);
    splitter_->setStretchFactor(0, 0); // Give bookmark panel minimal space
    splitter_->setStretchFactor(1, 1); // Document view gets priority

    // Adjust the bookmark panel width to fit contents
    bookmark_panel_->adjust_width();

    // Determine content width, ensuring a reasonable minimum
    int content_width = std::max(bookmark_panel_->width() + 20, 100);

    // Set initial splitter sizes: Bookmark panel gets calculated width, rest goes to document view
    splitter_->setSizes({content_width, width() - content_width});

    setCentralWidget(splitter_);

    create_menus();
    create_toolbar();
    create_status_bar();

    setWindowIcon(QIcon(":/MusicReader/images/gclef.png"));

    restore_window_state();

    // hides background image if there are open documents
    update_background();

    exit_button_ = new FullscreenExitButton(this);
    qApp->installEventFilter(this);

    if (config_.save_cadence_secs() > 0) {
        QTimer* autosave_timer = new QTimer(this);
        // cast to void just to avoid warning that we are discarding the return value. ugh.
        connect(autosave_timer, &QTimer::timeout, this, [this]() {
            (void)QtConcurrent::run([this]() {
                for (int i = 0; i < tab_widget_->count(); ++i) {
                    auto doc = document_at(i);
                    if (doc) {
                        doc->save();
                    }
                }
            });
        });

        autosave_timer->start(config_.save_cadence_secs() * 1000);
    }

    create_global_shortcuts();


    if (config_.restore_documents())
        QTimer::singleShot(0, this, [this]() {
            restore_open_documents();
        });

    // setup_mouse_hiding();
    //  this will search the directories and create the fast search dialog
    //  asynchronously, because it can take many seconds to populate all the
    //  files.
    initialize_fast_search();

    setAcceptDrops(true);

    // Monitor screen changes
    for (QScreen* screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::geometryChanged, this, &MusicReader::on_screen_geometry_changed);
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, &MusicReader::on_screen_dpi_changed);
    }

    // Handle new screens being added
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        connect(screen, &QScreen::geometryChanged, this, &MusicReader::on_screen_geometry_changed);
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, &MusicReader::on_screen_dpi_changed);
    });

    // Show tour prompt for first-time users
    QTimer::singleShot(500, this, [this]() {
        check_first_run_tour();
    });
}


void MusicReader::on_document_loaded(std::string name, int page)
{
    auto i = doc_is_open(name);
    if (i.has_value()) {
        auto viewer = viewer_tab(i.value());
        if (viewer)
            viewer->get_page(page);
    }
}


void MusicReader::create_global_shortcuts()
{
    // Make global keyboard shortcuts within the app
    // not all are set here. Ones that have a menu item need to set the
    // shortcut there, then make it global by calling addAction(some_menu_action_);

    QShortcut* shortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::add_bookmark);

    shortcut = new QShortcut(QKeySequence("PageUp"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::on_page_up);

    shortcut = new QShortcut(Qt::Key_B, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::MusicReader::add_bookmark);

    shortcut = new QShortcut(QKeySequence("Ctrl+G"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::goto_page_dialog);

    shortcut = new QShortcut(Qt::Key_1, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        set_page_view_count(1);
    });

    shortcut = new QShortcut(Qt::Key_2, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        set_page_view_count(2);
    });

    shortcut = new QShortcut(Qt::Key_S, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::toggle_page_step);

    shortcut = new QShortcut(Qt::Key_O, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::open_file_dialog_default_path);

    shortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::open_fast_search_dialog);

    shortcut = new QShortcut(Qt::Key_Z, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::toggle_page_zoom);

    shortcut = new QShortcut(Qt::Key_P, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::toggle_performance_mode);


    shortcut = new QShortcut(Qt::Key_Space, this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::on_page_down);

    shortcut = new QShortcut(QKeySequence("F5"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::reload_document);

    shortcut = new QShortcut(QKeySequence("F2"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::edit_document);

    /* // not ready for prime time yet!
    action = new QAction(QIcon(":/MusicReader/images/annotation.ico"), "Text Annotation", this);
    action->setToolTip("Text annotation mode (T)");
    action->setCheckable(true);
    connect(action, &QAction::triggered, this, &MusicReader::toggle_text_annotation_mode);
    toolbar_->addAction(action);
    text_annotation_action_ = action;

    // Add T shortcut
    shortcut = new QShortcut(Qt::Key_T, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::toggle_text_annotation_mode);
    */
}


void MusicReader::dragEnterEvent(QDragEnterEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->mimeData()->hasUrls()) {
        // Check if any URLs are PDF files
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QString file_path = url.toLocalFile();
                if (file_path.toLower().endsWith(".pdf")) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }
    event->ignore();
}


void MusicReader::dropEvent(QDropEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QString file_path = url.toLocalFile();
                if (file_path.toLower().endsWith(".pdf")) {
                    open_pdf_in_tab(file_path.toStdString());
                }
            }
        }
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}


bool MusicReader::eventFilter(QObject* watched, QEvent* event)
{
    SAFE_METHOD;

    // Handle mouse movement for cursor hiding
    if (event->type() == QEvent::MouseMove) {
        if (handle_mouse_movement(watched, event))
            return true;
    }
    // handle fullscreen logic
    if (event->type() == QEvent::MouseMove && isFullScreen()) {
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


bool MusicReader::handle_mouse_movement(QObject* watched, QEvent* event)
{
    SAFE_METHOD;

    Q_UNUSED(watched);
    Q_UNUSED(event);

    // Show cursor if it was hidden
    if (cursor_hidden_) {
        QApplication::restoreOverrideCursor();
        cursor_hidden_ = false;
    }

    // Reset the timer
    if (mouse_hide_timer_)
        mouse_hide_timer_->start(3000); // 3 seconds

    // Return false to allow event propagation
    return false;
}


void MusicReader::setup_mouse_hiding()
{
    SAFE_METHOD;

    // Create timer for hiding the mouse cursor after inactivity
    mouse_hide_timer_ = new QTimer(this);
    mouse_hide_timer_->setSingleShot(true);
    connect(mouse_hide_timer_, &QTimer::timeout, this, [this]() {
        // Only hide cursor if we're over a document
        PDFViewer* current = current_viewer();
        if (current) {
            QRect viewerRect = current->rect();
            QPoint globalPos = QCursor::pos();
            QPoint localPos = current->mapFromGlobal(globalPos);

            if (viewerRect.contains(localPos)) {
                QApplication::setOverrideCursor(Qt::BlankCursor);
                cursor_hidden_ = true;
            }
        }
    });

    mouse_hide_timer_->start(HIDE_MOUSE_TIMEOUT_MS);
}


void MusicReader::reset_cursor_timer()
{
    SAFE_METHOD;
    if (!mouse_hide_timer_)
        return;

    if (cursor_hidden_) {
        QApplication::restoreOverrideCursor();
        cursor_hidden_ = false;
    }
    mouse_hide_timer_->start(HIDE_MOUSE_TIMEOUT_MS);
}


void MusicReader::closeEvent(QCloseEvent* event)
{
    SAFE_METHOD;

    load_manager_.stop_loading();
    DevStatusDialog::close_if_open();
    save_window_state_to_config();
    save_config();

    QMainWindow::closeEvent(event); // Call base class implementation
    check_for_errors_on_exit();
}


void MusicReader::save_window_state_to_config()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    ConfigFileGroupSave group_saver(config_);

    // Save the currently open tab index
    config_.set_open_tab(tab_widget_->currentIndex());

    // Ensure position values are non-negative to prevent config errors
    int x = std::max(0, pos().x());
    int y = std::max(0, pos().y());
    int width = size().width();
    int height = size().height();

    config_.set_app_size({x, y, width, height});

    // Save the toolbar location (Qt enum values match ToolbarLocation)
    config_.set_toolbar_location(static_cast<ToolbarLocation>(toolBarArea(toolbar_)));
}


void MusicReader::check_for_errors_on_exit()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (logger::logged_error()) {
        QMessageBox msg_box(this);
        msg_box.setWindowTitle("Internal Errors");
        msg_box.setText("There were internal errors");
        msg_box.setIcon(QMessageBox::Warning);

        QPushButton* ok_button = msg_box.addButton("Ignore", QMessageBox::AcceptRole);
        QPushButton* view_errors_button = msg_box.addButton("View errors", QMessageBox::ActionRole);
        msg_box.setDefaultButton(view_errors_button);

        msg_box.exec();

        if (msg_box.clickedButton() == view_errors_button)
            show_log_content();
        else if (msg_box.clickedButton() == ok_button)
            msg_box.close();
    }
}

void MusicReader::show_log_content()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

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
    QPushButton ok_button("OK");
    QObject::connect(&ok_button, &QPushButton::clicked, &dialog, &QDialog::accept);

    button_layout.addStretch();
    button_layout.addWidget(&ok_button);

    layout.addLayout(&button_layout);

    dialog.setLayout(&layout);
    dialog.resize(800, 600);
    dialog.exec();
}


void MusicReader::create_file_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // File menu
    QMenu* file_menu = menu_bar->addMenu("&File");

    QAction* action = new QAction("&Open...", this);
    action->setShortcut(shortcuts_["open_file"]);
    file_menu->addAction(action);
    connect(action, &QAction::triggered, this, &MusicReader::open_file_dialog_default_path);

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

    file_menu->addSeparator();

    action = new QAction("Copy log to clipboard", this);
    action->setShortcut(shortcuts_["open_file"]);
    connect(action, &QAction::triggered, this, &MusicReader::copy_log_to_clipboard);
    file_menu->addAction(action);

    file_menu->addSeparator();

    QAction* exit_action = new QAction("E&xit", this);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);
    file_menu->addAction(exit_action);
}


void MusicReader::create_edit_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    edit_menu_ = menu_bar->addMenu("&Edit");

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

    edit_menu_->addSeparator();

    QAction* set_bookmarks_action = new QAction("Set bookmarks from txt file", this);
    connect(set_bookmarks_action, &QAction::triggered, this, &MusicReader::set_bookmarks_from_file);
    edit_menu_->addAction(set_bookmarks_action);
    QAction* save_bookmarks_action = new QAction("Save bookmarks to txt file", this);
    connect(save_bookmarks_action, &QAction::triggered, this, &MusicReader::save_bookmarks_to_file);
    edit_menu_->addAction(save_bookmarks_action);

    connect(edit_menu_, &QMenu::aboutToShow, this, [this]() {
        auto doc = current_document();
        undo_action_->setEnabled(doc && doc->can_undo());
        redo_action_->setEnabled(doc && doc->can_redo());
    });
}


void MusicReader::set_bookmarks_from_file()
{
    auto doc = current_document();
    if (doc && doc->set_bookmarks_from_txt_file())
        update_bookmarks_for_doc();
}



void MusicReader::save_bookmarks_to_file()
{
    auto doc = current_document();
    if (doc) {
        doc->save_bookmarks_to_txt_file();
    }
}


void MusicReader::create_view_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMenu* view_menu = menu_bar->addMenu("&View");

    bookmark_menu_action_ = new QAction("Show Bookmarks", this);
    bookmark_menu_action_->setShortcut(QKeySequence("Ctrl+B"));
    bookmark_menu_action_->setShortcutContext(Qt::ApplicationShortcut);
    bookmark_menu_action_->setCheckable(true);
    bookmark_menu_action_->setChecked(true);
    connect(bookmark_menu_action_, &QAction::triggered, this, &MusicReader::toggle_bookmark_panel);
    view_menu->addAction(bookmark_menu_action_);

    tabs_menu_action_ = new QAction("Show Document &Tabs", this);
    tabs_menu_action_->setShortcut(Qt::Key_T);
    tabs_menu_action_->setShortcutContext(Qt::ApplicationShortcut);

    tabs_menu_action_->setCheckable(true);
    tabs_menu_action_->setChecked(tabs_visible_);
    connect(tabs_menu_action_, &QAction::triggered, this, &MusicReader::toggle_tab_visibility);
    view_menu->addAction(tabs_menu_action_);

    // make it global so it works even when this window doesn't have focus
    addAction(tabs_menu_action_);

    menubar_menu_action_ = new QAction("&Menu Bar", this);
    menubar_menu_action_->setCheckable(true);
    menubar_menu_action_->setChecked(config_.show_menu());
    connect(menubar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_menu_visibility);
    view_menu->addAction(menubar_menu_action_);

    toolbar_menu_action_ = new QAction("Tool Bar", this);
    toolbar_menu_action_->setShortcut(shortcuts_["toolbar"]);
    toolbar_menu_action_->setCheckable(true);
    toolbar_menu_action_->setChecked(true);
    connect(toolbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_toolbar_visibility);
    view_menu->addAction(toolbar_menu_action_);

    statusbar_menu_action_ = new QAction("&Status Bar", this);
    statusbar_menu_action_->setCheckable(true);
    statusbar_menu_action_->setChecked(config_.show_status_bar());
    connect(statusbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_statusbar_visibility);
    view_menu->addAction(statusbar_menu_action_);

    if (config_.in_dev_mode()) {
        view_menu->addSeparator();
        auto* dev_action = new QAction("Show &Developer Status Dialog...", this);
        connect(dev_action, &QAction::triggered, this, &MusicReader::open_dev_status_dialog);
        view_menu->addAction(dev_action);
    }

    auto* action = new QAction("View Log...", this);
    connect(action, &QAction::triggered, this, &MusicReader::show_log_file);
    view_menu->addAction(action);

    QAction* goto_action = new QAction("&Goto Page...", this);
    goto_action->setShortcut(QKeySequence("Ctrl+G"));
    connect(goto_action, &QAction::triggered, this, &MusicReader::goto_page_dialog);
    view_menu->addAction(goto_action);

    view_menu->addSeparator();

    QAction* tour_action = new QAction("Tour...", this);
    connect(tour_action, &QAction::triggered, this, &MusicReader::open_tour_dialog);
    view_menu->addAction(tour_action);
}


void MusicReader::create_imslp_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMenu* imslp_menu = menu_bar->addMenu("&IMSLP");

    QAction* action = new QAction("&Search...", this);
    action->setShortcut(QKeySequence("I"));
    connect(action, &QAction::triggered, this, &MusicReader::open_imslp_search_dialog);
    imslp_menu->addAction(action);
}


void MusicReader::create_menus()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMenuBar* menu_bar = menuBar();

    create_file_menu(menu_bar);
    create_edit_menu(menu_bar);
    create_imslp_menu(menu_bar);
    create_view_menu(menu_bar);
    create_help_menu(menu_bar);

    menuBar()->setStyleSheet(R"(
        QMenu::item {
            font-weight: normal;
        }
        QMenu::item:disabled {
            color: gray;
        })");

    if (!config_.show_menu())
        menu_bar->hide();
}


void MusicReader::create_help_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMenu* help_menu = menu_bar->addMenu("&Help");

    QAction* action = new QAction("&About MusicReader...", this);
    connect(action, &QAction::triggered, this, &MusicReader::show_about_dialog);
    help_menu->addAction(action);
}


void MusicReader::show_about_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QDialog dialog(this);
    dialog.setWindowTitle("About MusicReader");
    dialog.setModal(true);

    QVBoxLayout layout(&dialog);

    QLabel* title = new QLabel("MusicReader", &dialog);
    title->setAlignment(Qt::AlignCenter);
    QFont title_font = title->font();
    title_font.setPointSize(16);
    title_font.setBold(true);
    title->setFont(title_font);
    layout.addWidget(title);

    QLabel* version = new QLabel("Version 1.0", &dialog);
    version->setAlignment(Qt::AlignCenter);
    layout.addWidget(version);

    QLabel* copyright_info = new QLabel("&copy; 2025 Roger Labbe. All rights reserved.<br>"
                                        "github.com/rlabbe/MusicReader",
                                        &dialog);
    copyright_info->setAlignment(Qt::AlignCenter);
    copyright_info->setTextFormat(Qt::RichText);
    layout.addWidget(copyright_info);

    layout.addSpacing(20);

    QLabel* info = new QLabel(&dialog);
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    info->setText("PDF music reader application with IMSLP integration.<br><br>"
                  "This application is free and open source software.<br><br>"
                  "<b>License:</b> GNU Affero General Public License v3.0 (AGPL V3)<br><br>"
                  "<b>Third-party libraries and data:</b>"
                  "<ul>"
                  "<li>Qt Framework - &copy; The Qt Company Ltd. Licensed under LGPL v3<br>https://www.qt.io/<br></li>"
                  "<li>MuPDF - &copy; Artifex Software, Inc. Licensed under AGPL v3<br>https://mupdf.com/<br></li>"
                  "<li>IMSLP (International Music Score Library Project).<br>Optional search integration for public "
                  "domain scores. Users must respect IMSLP's terms of service, copyright laws in the user's country, "
                  "and download limits.<br>https://imslp.org</li>"
                  "</ul>"
                  "Built with Qt " QT_VERSION_STR "<br><br>"
                  "Source code and license information available at github.com/rlabbe/MusicReader.");
    layout.addWidget(info);

    QHBoxLayout button_layout;
    QPushButton* ok_button = new QPushButton("OK", &dialog);
    connect(ok_button, &QPushButton::clicked, &dialog, &QDialog::accept);
    button_layout.addStretch();
    button_layout.addWidget(ok_button);
    layout.addLayout(&button_layout);

    dialog.exec();
}


void MusicReader::update_recent_files_list()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    open_recent_menu_->clear();

    for (const auto& path : std::views::reverse(config_.recent_documents())) {
        QString display_text = QString::fromStdWString(path.wstring());
        QString tooltip_text = QString::fromStdWString(path.wstring());

        QAction* action = new QAction(display_text, this);
        action->setToolTip(tooltip_text);

        connect(action, &QAction::triggered, this, [this, path]() {
            open_pdf_in_tab(path.string());
        });

        open_recent_menu_->addAction(action);
    }
}


void MusicReader::show_context_menu(const QPoint& pos)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto viewer = current_viewer();
    if (!viewer)
        return;
    if (viewer->in_page_break_edit_mode())
        return; // don't show menu in this mode, right-click is used for deleting

    QMenu context_menu(this);

    QAction* reload_action = new QAction("Reload", this);
    reload_action->setShortcut(QKeySequence("F5"));
    connect(reload_action, &QAction::triggered, this, &MusicReader::reload_document);

    QAction* edit_action = new QAction("Edit...", this);
    edit_action->setShortcut(QKeySequence("F2"));
    connect(edit_action, &QAction::triggered, this, &MusicReader::edit_document);

    QAction* open_folder_action = new QAction("Open from containing folder...", this);
    connect(open_folder_action, &QAction::triggered, this, &MusicReader::open_file_dialog_default_path);

    QAction* browse_folder_action = new QAction("Browse containing folder...", this);
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
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto doc = current_document();
    if (!doc)
        return;

    auto file_path = QString::fromStdString(doc->filename());
    QFileInfo file_info(file_path);
    if (!file_info.exists())
        return;

    QString folder_path = file_info.absolutePath();
    QProcess::startDetached("explorer", {folder_path});
}


void MusicReader::show_log_file()
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    if (!m_logViewer) {
        m_logViewer = new FileViewer(logger::log_file_path(), this);
        connect(m_logViewer, &QObject::destroyed, this, [this]() {
            m_logViewer = nullptr;
        });
    }
    m_logViewer->show();
    m_logViewer->raise();
    m_logViewer->activateWindow();
}


bool MusicReader::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    SAFE_METHOD;

#ifdef Q_OS_WIN

    MSG* msg = static_cast<MSG*>(message);

    if (!config_.show_menu()) {
        if (msg->message == WM_SYSCOMMAND && (msg->wParam & 0xFFF0) == SC_MOUSEMENU) {
            show_titlebar_menu();
            *result = 0;
            return true; // Prevents Windows from showing its own menu
        }
    }

    if (msg->message == WM_POWERBROADCAST) {
        if (msg->wParam == PBT_APMSUSPEND) {
            was_suspended = true;
            logger::debug("System going to sleep");
        } else if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
            if (was_suspended) {
                QTimer::singleShot(500, this, [this]() {
                    force_redraw_all_viewers();
                });
                was_suspended = false;
            }
            logger::debug("System resumed from sleep");
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}


void MusicReader::reload_document()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto viewer = current_viewer();
    auto doc = current_document();
    if (!viewer || !doc)
        return;

    int page_num = viewer->current_page();
    doc->kill_load();
    load_manager_.remove_document(doc->filename());

    open_pdf_in_tab(doc->filename(), page_num, viewer);

    // Tab probably didn't change, but this ensures everything gets redrawn - page numbers, bookmarks, etc
    on_tab_changed();
}


void MusicReader::edit_document()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto doc = current_document();
    if (!doc)
        return;

    auto filename = doc->filename();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(filename)));
}


void MusicReader::show_titlebar_menu()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMenu menu(this);

    create_file_menu(&menu);
    create_edit_menu(&menu);
    create_view_menu(&menu);


    // Separator before Exit
    menu.addSeparator();

    // Standalone Exit Action
    QAction* exit_action = new QAction("E&xit", this);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);
    menu.addAction(exit_action);

    menu.exec(QCursor::pos());
}


void MusicReader::add_bookmark()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!bookmark_panel_)
        return;

    bool visible = bookmark_panel_->isVisible();
    if (!visible) {
        bookmark_panel_->setVisible(true);
        bookmark_menu_action_->setChecked(true);
    } else
        bookmark_panel_->add_bookmark();
}


void MusicReader::create_toolbar()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    toolbar_ = new QToolBar("Toolbar");
    Qt::ToolBarArea area = Qt::LeftToolBarArea;
    switch (config_.toolbar_location()) {
        case ToolbarLocation::Top: area = Qt::TopToolBarArea; break;
        case ToolbarLocation::Bottom: area = Qt::BottomToolBarArea; break;
        case ToolbarLocation::Left: area = Qt::LeftToolBarArea; break;
        case ToolbarLocation::Right: area = Qt::RightToolBarArea; break;
        default: logger::error("Invalid toolbar location in config"); ;
    }
    addToolBar(area, toolbar_);

    {
        QAction* action = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), "", this);
        action->setToolTip("Search for file... (F)");
        connect(action, &QAction::triggered, this, &MusicReader::open_fast_search_dialog);
        toolbar_->addAction(action);

        action = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "", this);
        action->setToolTip("Open File...(O)");
        connect(action, &QAction::triggered, this, &MusicReader::open_file_dialog_default_path);
        toolbar_->addAction(action);
    }

    single_icon_ = style()->standardIcon(QStyle::SP_FileIcon);
    double_icon_ = create_double_icon();

    if (in_single_page_mode())
        view_toggle_action_ = new QAction(single_icon_, "", this);
    else
        view_toggle_action_ = new QAction(double_icon_, "", this);

    view_toggle_action_->setToolTip("View one/two pages (1/2)");
    connect(view_toggle_action_, &QAction::triggered, this, &MusicReader::on_toggle_view_mode);
    toolbar_->addAction(view_toggle_action_);

    page_by_1_icon_ = QIcon(":/MusicReader/images/page_by_1.ico");
    page_by_2_icon_ = QIcon(":/MusicReader/images/page_by_2.ico");

    page_step_action_ = new QAction(config_.page_step_size() == 1 ? page_by_1_icon_ : page_by_2_icon_, "", this);
    connect(page_step_action_, &QAction::triggered, this, &MusicReader::toggle_page_step);
    page_step_action_->setToolTip("Page Step (S)");
    toolbar_->addAction(page_step_action_);

    zoomin_icon_ = QIcon(":/MusicReader/images/zoomin.ico");
    zoomout_icon_ = QIcon(":/MusicReader/images/zoomout.ico");
    zoom_in_out_action_ = new QAction(config_.zoom_to_content() ? zoomout_icon_ : zoomin_icon_, "", this);
    connect(zoom_in_out_action_, &QAction::triggered, this, &MusicReader::toggle_page_zoom);
    zoom_in_out_action_->setToolTip("Toggle zoom to content (Z)");
    toolbar_->addAction(zoom_in_out_action_);

    page_break_edit_action_ = new QAction(QIcon(":/MusicReader/images/page_break.ico"), "", this);
    page_break_edit_action_->setToolTip("Edit Page Breaks");
    page_break_edit_action_->setCheckable(true);
    connect(page_break_edit_action_, &QAction::triggered, this, &MusicReader::toggle_page_break_edit_mode);
    toolbar_->addAction(page_break_edit_action_);

    performance_mode_action_ = new QAction(QIcon(":/MusicReader/images/perform.ico"), "", this);
    performance_mode_action_->setToolTip("Toggle Performance Mode (P)");
    performance_mode_action_->setCheckable(true);
    connect(performance_mode_action_, &QAction::triggered, this, &MusicReader::toggle_performance_mode);
    toolbar_->addAction(performance_mode_action_);

    {
        auto* action = new QAction(QIcon(QPixmap(":/MusicReader/images/gear.png")), "", this);
        action->setToolTip("Settings");
        connect(action, &QAction::triggered, this, &MusicReader::open_config_dialog);
        toolbar_->addAction(action);
    }

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolbar_->addWidget(spacer);

    toolbar_page_selector_ = new QComboBox();
    toolbar_page_selector_->setEditable(false);
    toolbar_page_selector_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    toolbar_page_selector_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toolbar_page_selector_->setMinimumContentsLength(1);

    QWidget* page_container = new QWidget();
    QHBoxLayout* page_layout = new QHBoxLayout(page_container);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->addStretch();
    page_layout->addWidget(toolbar_page_selector_);
    page_layout->addStretch();
    page_container->setMaximumWidth(toolbar_page_selector_->sizeHint().width());
    toolbar_->addWidget(page_container);

    connect(toolbar_page_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MusicReader::on_toolbar_page_changed);

    toolbar_clock_ = new QLCDNumber(5);
    toolbar_clock_->setSegmentStyle(QLCDNumber::Flat);
    toolbar_clock_->setFrameStyle(QFrame::NoFrame);
    toolbar_clock_->setFixedSize(60, 20);

    QWidget* clock_container = new QWidget();
    QHBoxLayout* clock_layout = new QHBoxLayout(clock_container);
    clock_layout->setContentsMargins(0, 0, 0, 0);
    clock_layout->addStretch();
    clock_layout->addWidget(toolbar_clock_);
    clock_layout->addStretch();
    clock_container->setMaximumWidth(toolbar_clock_->sizeHint().width());
    toolbar_->addWidget(clock_container);

    QTimer* clock_timer = new QTimer(this);
    connect(clock_timer, &QTimer::timeout, this, [this]() {
        toolbar_clock_->display(QTime::currentTime().toString("HH:mm"));
    });
    clock_timer->start(1000);
    toolbar_clock_->display(QTime::currentTime().toString("HH:mm"));
}


void MusicReader::set_page_view_count(int count)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (count < 1 || count > 2) {
        logger::error("Invalid page view count: {}", count);
        return;
    }
    config_.set_page_view_count(count);
    view_toggle_action_->setIcon(config_.page_view_count() == 1 ? single_icon_ : double_icon_);

    refresh_all_documents();
}


void MusicReader::on_toggle_view_mode()
{
    SAFE_METHOD;

    ConfigFileGroupSave group_saver(config_);

    // This method will trigger a bunch of signals to select the current page, causing
    // the bookmark panel's selection to jump around if tracking page changes, so
    // turn if off while we update everything, then turn it on and select the current page.
    bookmark_panel_->pause_tracking();

    // Toggle between single and double page view mode
    config_.set_page_view_count((config_.page_view_count() == 1) ? 2 : 1);

    // Update the icon
    view_toggle_action_->setIcon(config_.page_view_count() == 1 ? single_icon_ : double_icon_);

    refresh_all_documents();
    bookmark_panel_->resume_tracking();

    // Trigger bookmark selection once after transition is complete
    if (auto viewer = current_viewer())
        bookmark_panel_->select_page(viewer->current_page());
}


bool MusicReader::in_single_page_mode() const
{
    TRACE_FUNCTION;
    return config_.page_view_count() == 1;
}


void MusicReader::toggle_tab_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    WaitCursor wc;
    tabs_visible_ = !tabs_visible_;

    if (tab_widget_)
        tab_widget_->tabBar()->setVisible(tabs_visible_);

    if (tabs_menu_action_)
        tabs_menu_action_->setChecked(tabs_visible_);
}


void MusicReader::toggle_page_zoom()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    bookmark_panel_->pause_tracking();

    // Toggle zoom setting and save config
    config_.set_zoom_to_content(!config_.zoom_to_content());
    config_.save();

    // Update the icon
    QIcon icon = config_.zoom_to_content() ? zoomout_icon_ : zoomin_icon_;
    zoom_in_out_action_->setIcon(icon);

    refresh_all_documents();
    bookmark_panel_->resume_tracking();
}


void MusicReader::toggle_page_step()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    config_.toggle_page_step_size();

    // Update the icon
    if (config_.page_step_size() == 1)
        page_step_action_->setIcon(page_by_1_icon_);
    else
        page_step_action_->setIcon(page_by_2_icon_);
}


PDFViewer* MusicReader::current_tab() const
{
    SAFE_METHOD;

    if (!tab_widget_)
        return nullptr;

    int i = tab_widget_->currentIndex();
    if (i >= 0) {
        QWidget* tab_widget = tab_widget_->widget(i);
        for (QObject* child : tab_widget->children()) {
            if (auto* viewer = qobject_cast<PDFViewer*>(child))
                return viewer;
        }
    }
    return nullptr;
}


std::shared_ptr<Document> MusicReader::current_document(const std::string& log_msg) const
{
    SAFE_METHOD;

    PDFViewer* tab = current_tab();
    if (tab)
        return tab->document();

    if (!log_msg.empty())
        logger::error(log_msg);
    return nullptr;
}


std::pair<int, bool> MusicReader::current_page(const std::string& log_msg) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    PDFViewer* viewer = current_viewer(log_msg);
    if (viewer)
        return {viewer->current_page(), true};

    if (!log_msg.empty())
        logger::error(log_msg);
    return {1, false};
}


std::string MusicReader::current_document_name() const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto doc = current_document();
    return doc ? doc->filename() : "";
}


std::shared_ptr<Document> MusicReader::document_at(int index) const
{
    SAFE_METHOD;

    PDFViewer* tab = viewer_tab(index);
    return tab ? tab->document() : nullptr;
}


PDFViewer* MusicReader::viewer_tab(int index) const
{
    SAFE_METHOD;

    try {
        QWidget* tab = tab_widget_->widget(index);
        for (QObject* child : tab->children()) {
            if (auto* viewer = qobject_cast<PDFViewer*>(child))
                return viewer;
        }
    } catch (...) {
        return nullptr;
    }
    return nullptr;
}


void MusicReader::save_config()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    save_open_documents_to_config();
    update_logging_level();
}


void MusicReader::save_open_documents_to_config()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::vector<OpenDocument> open_documents;
    open_documents.reserve(tab_widget_->count());
    for (int index = 0; index < tab_widget_->count(); ++index) {
        auto pdf_viewer = tab_widget_->widget(index)->findChild<PDFViewer*>();
        if (pdf_viewer) {
            auto doc = pdf_viewer->document();
            bool is_temporary = doc->is_temporary();

            if (is_temporary)
                continue;

            auto name = doc->filename();

            // Find existing document to preserve access_order
            for (const auto& existing_doc : config_.open_documents()) {
                if (existing_doc.filename == name) {
                    open_documents.push_back({
                        name, pdf_viewer->current_page(), doc->page_count(), existing_doc.access_order,
                        index // tab_order = current tab position
                    });
                    break;
                }
            }
        }
    }
    config_.set_open_documents(open_documents);
}


void MusicReader::on_tab_moved(int /*from*/, int /*to*/)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    save_open_documents_to_config();
}


void MusicReader::on_tab_changed()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    update_title();
    update_bookmark_panel();
    show_page_count();

    auto viewer = current_viewer();
    if (viewer) {
        viewer->update_status_bar();

        // Update page break edit button state based on current viewer
        page_break_edit_action_->setChecked(viewer->in_page_break_edit_mode());

        if (!restoring_documents_) {
            auto doc = current_document();
            if (doc) {
                config_.update_document_access(doc->filename());
                update_document_priority_order();
            }
        }
    }
}


void MusicReader::on_close_tab(int index)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto doc = document_at(index);
    if (doc) {
        doc->kill_load();
        load_manager_.remove_document(doc->filename());
        config_.add_recent_document(doc->filename());
    }

    QWidget* widget_to_remove = tab_widget_->widget(index);
    if (widget_to_remove) {
        widget_to_remove->deleteLater();
        tab_widget_->removeTab(index);
    }

    save_open_documents_to_config();
    on_tab_changed();

    QTimer::singleShot(1000, this, [this] {
        update_memory_usage();
    });
}


void MusicReader::update_title(int index)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    index;

    if (tab_widget_->count() > 0) {
        QString current_tab_title = tab_widget_->tabText(tab_widget_->currentIndex());
        setWindowTitle(current_tab_title);
    } else
        setWindowTitle("MusicReader");
}


void MusicReader::keyPressEvent(QKeyEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (event->key() == Qt::Key_F11) {
        if (isFullScreen()) {
            status_bar_->setVisible(config_.show_status_bar());
            config_.show_menu() ? menuBar()->show() : menuBar()->hide();
            showNormal();
        } else {
            menuBar()->hide();
            if (status_bar_)
                status_bar_->setVisible(false);
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
        case Qt::Key_PageUp:
            tab->page_up();
            event->accept();
            return;
        case Qt::Key_PageDown:
            tab->page_down();
            event->accept();
            return;
        case Qt::Key_Left:
            tab->change_page(-1);
            event->accept();
            return;
        case Qt::Key_Right:
            tab->change_page(1);
            event->accept();
            return;
        default: QMainWindow::keyPressEvent(event);
    }
}


void MusicReader::on_page_up()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto tab = current_viewer();
    if (tab)
        tab->page_up();
}


void MusicReader::on_page_down()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto tab = current_viewer();
    if (tab)
        tab->page_down();
}


PDFViewer* MusicReader::current_viewer(const std::string& log_err) const
{
    SAFE_METHOD;
    try {
        PDFViewer* tab = current_tab();
        if (tab)
            return tab;

        if (!log_err.empty())
            logger::error(log_err);
    } catch (...) {
        if (!log_err.empty())
            logger::error(log_err);
    }
    return nullptr;
}


void MusicReader::create_bookmark_panel()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    bookmark_panel_ = new BookmarkPanel(this);
    [[maybe_unused]] bool s =
        connect(bookmark_panel_, &BookmarkPanel::bookmark_clicked, this, &MusicReader::go_to_bookmark);

    connect(bookmark_panel_, &BookmarkPanel::bookmark_visibility_changed, this,
            &MusicReader::update_menu_bookmark_visibility);
}


void MusicReader::update_bookmark_panel()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    bookmark_panel_->populate();
    update_background();
}


void MusicReader::update_bookmarks_for_doc()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    bookmark_panel_->populate();
    update_background();
}


QIcon MusicReader::create_double_icon()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

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
    SAFE_METHOD;
    // TRACE_FUNCTION;

    for (int i = 0; i < tab_widget_->count(); ++i) {
        auto widget = viewer_tab(i);
        if (widget && widget->document()->filename() == name)
            return i;
    }

    return std::nullopt;
}


void MusicReader::go_to_bookmark(int page_num)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (page_num > 0) {
        auto* viewer = current_viewer();
        if (viewer) {
            // Tell renderer to go to the physical page, then refresh display
            viewer->goto_physical_page(page_num);
        } else
            logger::error("No viewer to navigate to bookmark");
    }
}


void MusicReader::update_menu_bookmark_visibility()
{
    // TODO
    SAFE_METHOD;
}


void MusicReader::refresh_all_documents()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Handle current tab first for responsiveness
    PDFViewer* current = current_viewer();
    if (current)
        current->refresh();

    // Then update other tabs in the background
    for (int i = 0; i < tab_widget_->count(); ++i) {
        PDFViewer* viewer = viewer_tab(i);
        if (viewer && viewer != current)
            viewer->refresh();
    }
}


void MusicReader::open_config_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    try {
        ConfigDialog editor_dialog(config_, this);
        save_open_documents_to_config();

        editor_dialog.exec();

        if (editor_dialog.result() == QDialog::Accepted) {
            config_.save();
            on_config_saved();
            update_logging_level();
        }
    } catch (const std::exception& e) {
        logger::error("Failed to open settings dialog: " + std::string(e.what()));
    }
}


void MusicReader::update_logging_level()
{
    SAFE_METHOD;
    // turning on tracing here can conflict with the below as trace uses the function_tracer class
    // TRACE_FUNCTION;
    switch (config_.log_level()) {
        case LogLevel::Diagnostic: logger::enable_debug_logging(true); break;
        case LogLevel::Trace: logger::enable_trace_logging(true); break;
        default: logger::enable_debug_logging(false); break;
    }
}


void MusicReader::restore_window_state()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (config_.restore_window_position()) {
        try {
            const auto& app_size = config_.app_size();
            if (app_size.size() >= 4) {
                move(app_size[0], app_size[1]);
                resize(app_size[2], app_size[3]);
                ensure_window_is_visible(this);
            }
        } catch (...) {
            logger::error("Failed to restore app position and size");
        }
    }
}


void MusicReader::open_dev_status_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    DevStatusDialog::show(config_, this);
}


void MusicReader::open_imslp_search_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    try {
        auto* dialog = new IMSLPSearchDialog(this);
        dialog->show();
    } catch (const std::exception& e) {
        logger::error("Failed to open IMSLP search dialog: " + std::string(e.what()));
        display_error_message("Failed to open IMSLP search dialog: " + std::string(e.what()));
    }
}


void MusicReader::open_file_dialog_default_path()
{
    open_file_dialog("");
}


void MusicReader::open_file_dialog(const std::filesystem::path& pathname)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::filesystem::path default_directory;

    if (!pathname.empty()) {
        default_directory = pathname;
    } else {
        auto doc = current_document();
        if (doc)
            default_directory = std::filesystem::path(doc->filename()).parent_path();
    }

    QStringList filenames = QFileDialog::getOpenFileNames(
        this, "Open PDF", QString::fromStdU16String(default_directory.u16string()), "PDF Files (*.pdf)");

    if (!filenames.isEmpty()) {
        for (const QString& name : filenames)
            open_pdf_in_tab(name.toStdU16String());
    }
}


PDFViewer* MusicReader::open_pdf_in_tab(const std::filesystem::path& filename, int page, PDFViewer* viewer,
                                        bool is_temporary)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!viewer) {
        if (auto i = doc_is_open(filename); i.has_value()) {
            focus_on_tab(i.value());
            return nullptr;
        }
    }

    auto doc = open_pdf_document(filename, page);
    if (!doc)
        return nullptr;

    doc->set_is_temporary(is_temporary);

    logger::info("Opened " + doc->filename());

    if (!viewer) {
        QWidget* tab = new QWidget();
        viewer = new PDFViewer(doc, &config_, page, status_bar_, tab, this, bookmark_panel_);
        connect(viewer, &PDFViewer::page_changed, this, &MusicReader::on_viewer_page_changed, Qt::UniqueConnection);

        QVBoxLayout* layout = new QVBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(viewer);
        tab->setLayout(layout);

        int index = tab_widget_->addTab(tab, QString::fromStdU16String(filename.stem().u16string()));
        tab_widget_->setTabToolTip(index, QString::fromStdU16String(filename.u16string()));
        tab_widget_->setCurrentWidget(tab);

        focus_on_tab(tab_widget_->currentIndex());
        config_.add_new_document(filename, page, doc->page_count());
    } else {
        viewer->replace_document(doc, page);
        config_.update_document_access(filename);
    }

    save_open_documents_to_config();

    load_manager_.add_document(doc);

    viewer->refresh();
    QTimer::singleShot(5000, this, [this] {
        update_memory_usage();
    });

    return viewer;
}


void MusicReader::goto_page_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto viewer = current_viewer();
    if (!viewer)
        return;

    int max_page = viewer->page_count();

    QDialog dialog(this);
    dialog.setWindowTitle("Go to Page");
    dialog.setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* label = new QLabel(QString("Enter page number (1 - %1):").arg(max_page), &dialog);
    layout->addWidget(label);

    QLineEdit* line_edit = new QLineEdit(&dialog);
    line_edit->setValidator(new QIntValidator(1, max_page, line_edit));
    layout->addWidget(line_edit);

    QDialogButtonBox* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(button_box);

    QObject::connect(button_box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(button_box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        bool ok = false;
        int page = line_edit->text().toInt(&ok);
        if (ok && page >= 1 && page <= max_page) {
            viewer->get_page(page);
        }
    }
}


std::shared_ptr<Document> MusicReader::open_pdf_document(const std::filesystem::path& filename, int page_num)
{
    LOG_EXCEPTION;
    TRACE_FUNCTION;

    if (!std::filesystem::exists(filename)) {
        logger::debug(filename.u8string() + u8" doesn't exist");
        return {};
    }

    auto doc = std::make_shared<Document>(filename, config_.dpi(), page_num);
    connect(doc.get(), &Document::bookmarks_loaded, this, [&]() {
        bookmark_panel_->populate();
        update_background();
    });
    load_manager_.add_document(doc);

    return doc;
}


void MusicReader::focus_on_tab(int index)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    PDFViewer* viewer = viewer_tab(index);
    if (viewer) {
        tab_widget_->setCurrentIndex(index);
        viewer->setFocusPolicy(Qt::StrongFocus);
        viewer->setFocus();
    }
}


void MusicReader::display_error_message(const std::string& msg)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMessageBox mbox;
    mbox.setIcon(QMessageBox::Critical);
    mbox.setText(QString::fromStdString(msg));
    mbox.setWindowTitle("Error");
    mbox.setStandardButtons(QMessageBox::Ok);
    mbox.exec();
}


bool MusicReader::display_query(const std::string& msg)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(QString::fromStdString(msg));
    msgBox.setWindowTitle("Confirmation");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    return msgBox.exec() == QMessageBox::Yes;
}


void MusicReader::create_status_bar()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    status_bar_ = new StatusBar();
    setStatusBar(status_bar_);
    status_bar_->setVisible(config_.show_status_bar());

    // Display initial memory usage
    update_memory_usage();

    // Start timer to update memory usage every 30 seconds
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MusicReader::update_memory_usage);
    timer_->start(30'000);
}


void MusicReader::update_memory_usage()
{
    SAFE_METHOD;
    // TRACE_FUNCTION;

    auto format_memory = [](size_t bytes) -> std::string {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
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
    SAFE_METHOD;
    TRACE_FUNCTION;

    PDFViewer* viewer = current_viewer();
    if (!viewer) {
        toolbar_page_selector_->blockSignals(true);
        toolbar_page_selector_->clear();
        toolbar_page_selector_->adjustSize();
        toolbar_page_selector_->blockSignals(false);
        return;
    }

    auto page_displays = viewer->get_page_displays();
    auto current_display = viewer->current_page_display();

    toolbar_page_selector_->blockSignals(true);
    toolbar_page_selector_->clear();
    for (const auto& display : page_displays)
        toolbar_page_selector_->addItem(QString::fromStdString(display));

    int index = toolbar_page_selector_->findText(QString::fromStdString(current_display));
    if (index >= 0)
        toolbar_page_selector_->setCurrentIndex(index);

    toolbar_page_selector_->adjustSize();
    toolbar_page_selector_->blockSignals(false);
}


void MusicReader::on_page_selected(int index)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    PDFViewer* viewer = current_viewer();
    if (!viewer)
        return;

    viewer->get_page(index);
}


void MusicReader::on_toolbar_page_changed(int index_0_based)
{
    PDFViewer* viewer = current_viewer();
    if (!viewer)
        return;

    viewer->get_page(index_0_based + 1);
}


void MusicReader::on_viewer_page_changed(int /*page_num*/)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    show_page_count();
}


void MusicReader::initialize_fast_search()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::string name = config_.music_directory().string();

    QTimer::singleShot(100, this, [this, name]() {
        FastFileSearchDialog::initialize_data(name);
        auto dsize = config_.fast_search_dialog_size();
        QRect size(dsize[0], dsize[1], dsize[2], dsize[3]);

        // Use mutex to safely initialize
        {
            std::lock_guard<std::mutex> lock(fast_search_mutex_);

            // Create dialog - constructor will load and prepare file display while hidden
            fast_search_dialog_ = new FastFileSearchDialog(this, config_, size);
            fast_search_initialized_ = true;
        }
        logger::debug("initialize_fast_search done");
    });
}


void MusicReader::open_fast_search_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    {
        std::unique_lock<std::mutex> lock(fast_search_mutex_);
        if (!fast_search_initialized_)
            fast_search_cv_.wait(lock, [this]() {
                return fast_search_initialized_;
            });
    }

    try {
        tab_widget_->setEnabled(false);
        fast_search_dialog_->show_dialog();
        // fast_search_dialog_->exec();
    } catch (const std::exception& e) {
        logger::error("Failed to open fast search dialog: " + std::string(e.what()));
    }

    tab_widget_->setEnabled(true);

    // Save dialog geometry
    QRect geometry = fast_search_dialog_->geometry();
    std::array<int, 4> size = {geometry.x(), geometry.y(), geometry.width(), geometry.height()};

    {
        ConfigFileGroupSave group_saver(config_);
        config_.set_fast_search_dialog_size(size);
        config_.set_music_directory(fast_search_dialog_->path());
    }

    // Handle selected files
    auto [selected_files, filepath] = fast_search_dialog_->selected_files();
    if (selected_files.size() == 1 && selected_files[0] == "open") {
        open_file_dialog(filepath);
    } else {
        for (const auto& file : selected_files) {
            open_pdf_in_tab(file);
        }
    }
}


void MusicReader::update_background()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

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


template<class T> class SaveState {
private:
    T& var_;
    T old_value;

public:
    SaveState(T& var, T value)
        : var_(var)
    {
        old_value = var;
        var_ = value;
    }
    ~SaveState() { var_ = old_value; }
};


void MusicReader::restore_open_documents()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    REQUIRES(bookmark_panel_);
    REQUIRES(tab_widget_);

    logger::debug("Restoring open documents...");
    SaveState(restoring_documents_, true);

    const auto docs_info = config_.open_documents();
    int num_docs = static_cast<int>(docs_info.size());

    if (num_docs == 0) {
        update_background();
        return;
    }

    int tab_to_focus = config_.open_tab();

    {
        DocumentLoadManagerGuard guard;

        // Sort documents by tab_order to restore correct tab positions
        auto sorted_docs = docs_info;
        std::sort(sorted_docs.begin(), sorted_docs.end(), [](const OpenDocument& a, const OpenDocument& b) {
            return a.tab_order < b.tab_order;
        });

        std::vector<PDFViewer*> viewers;
        std::vector<std::shared_ptr<Document>> documents;

        for (auto doc_info : sorted_docs) {
            logger::debug("restoring document {} at page {} with tab_order {}", doc_info.filename.string(),
                          doc_info.page, doc_info.tab_order);
            auto doc = open_pdf_document(doc_info.filename, doc_info.page);
            if (!doc) {
                logger::info("Failed to re-open document: {}", doc_info.filename.string());
                continue;
            }
            documents.push_back(doc);

            QWidget* tab = new QWidget();
            PDFViewer* viewer = new PDFViewer(doc, &config_, doc_info.page, status_bar_, tab, this, bookmark_panel_);
            connect(viewer, &PDFViewer::page_changed, this, &MusicReader::on_viewer_page_changed, Qt::UniqueConnection);
            viewers.push_back(viewer);

            QVBoxLayout* layout = new QVBoxLayout();
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(viewer);
            tab->setLayout(layout);

            int index = tab_widget_->addTab(tab, QString::fromStdWString(doc_info.filename.stem().wstring()));
            tab_widget_->setTabToolTip(index, QString::fromStdWString(doc_info.filename.wstring()));
        }

        num_docs = (int)documents.size();
        if (num_docs == 0) {
            update_background();
            bookmark_panel_->adjust_width();
            config_.set_open_tab(-1);
            return;
        }

        // Focus on the tab that was active when saved, but ensure it's valid
        if (tab_to_focus >= 0 && tab_to_focus < tab_widget_->count()) {
            tab_widget_->setCurrentIndex(tab_to_focus);
        } else {
            // If saved tab index is invalid, focus on most recently accessed document
            auto most_recent =
                std::min_element(docs_info.begin(), docs_info.end(), [](const OpenDocument& a, const OpenDocument& b) {
                    return a.access_order < b.access_order;
                });
            if (most_recent != docs_info.end()) {
                // Find this document's tab index
                for (int i = 0; i < tab_widget_->count(); ++i) {
                    auto viewer = tab_widget_->widget(i)->findChild<PDFViewer*>();
                    if (viewer && viewer->document()->filename() == most_recent->filename) {
                        tab_widget_->setCurrentIndex(i);
                        break;
                    }
                }
            } else
                tab_widget_->setCurrentIndex(0);
        }
        QApplication::processEvents();
    }

    update_document_priority_order();

    logger::debug("cleaning up");
    update_document_priority_order();

    bookmark_panel_->adjust_width();
    update_background();
}


void MusicReader::update_document_priority_order()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::vector<std::filesystem::path> ordered_docs;

    auto current_doc = current_document();
    if (current_doc)
        ordered_docs.push_back(current_doc->filename());

    for (const auto& open_doc : config_.open_documents()) {
        std::filesystem::path doc_path = open_doc.filename;
        if (current_doc && doc_path == current_doc->filename())
            continue;

        if (doc_is_open(doc_path))
            ordered_docs.push_back(doc_path);
    }

    load_manager_.set_document_priority_order(ordered_docs);
}


void MusicReader::toggle_bookmark_panel()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    bool visible = !bookmark_panel_->isVisible();
    bookmark_panel_->setVisible(visible);
    bookmark_menu_action_->setChecked(visible);
}


void MusicReader::set_statusbar_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!status_bar_)
        return;
    if (!statusbar_menu_action_)
        return;

    bool visible = config_.show_status_bar();
    status_bar_->setVisible(visible);
    statusbar_menu_action_->setChecked(visible);
}


void MusicReader::toggle_statusbar_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    config_.set_show_status_bar(!config_.show_status_bar());
    set_statusbar_visibility();
}


void MusicReader::set_toolbar_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!toolbar_)
        return;
    if (!toolbar_menu_action_)
        return;

    bool visible = config_.show_toolbar();
    config_.set_show_toolbar(visible);
    toolbar_->setVisible(visible);
    toolbar_menu_action_->setChecked(visible);
}


void MusicReader::toggle_toolbar_visibility()
{
    TRACE_FUNCTION;
    SAFE_METHOD;

    config_.set_show_toolbar(!config_.show_toolbar());
    set_toolbar_visibility();
}


void MusicReader::toggle_menu_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    config_.set_show_menu(!config_.show_menu());
    set_menu_visibility();
}


void MusicReader::set_menu_visibility()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    config_.show_menu() ? menuBar()->show() : menuBar()->hide();
}


void MusicReader::on_config_saved()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    bookmark_panel_->pause_tracking();
    set_toolbar_visibility();
    refresh_all_documents();
    set_statusbar_visibility();
    set_menu_visibility();
    bookmark_panel_->resume_tracking();
}


void MusicReader::copy_log_to_clipboard()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!OpenClipboard(nullptr))
        return;
    EmptyClipboard();

    std::string contents = logger::get_log_content();

    HGLOBAL hglob = GlobalAlloc(GMEM_MOVEABLE, contents.size() + 1);
    if (!hglob) {
        CloseClipboard();
        return;
    }

    memcpy(GlobalLock(hglob), contents.c_str(), contents.size() + 1);
    GlobalUnlock(hglob);
    SetClipboardData(CF_TEXT, hglob);
    CloseClipboard();
}


void MusicReader::on_annotation_mode_changed(bool enabled)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    text_annotation_mode_ = enabled;
    text_annotation_action_->setChecked(enabled);
}


void MusicReader::toggle_text_annotation_mode()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    text_annotation_mode_ = !text_annotation_mode_;
    text_annotation_action_->setChecked(text_annotation_mode_);

    auto viewer = current_viewer();
    if (viewer)
        viewer->set_text_annotation_mode(text_annotation_mode_);
}


static inline std::string to_string(Qt::ApplicationState state)
{

    switch (state) {
        case Qt::ApplicationSuspended: return "Suspended";
        case Qt::ApplicationHidden: return "Hidden";
        case Qt::ApplicationInactive: return "Inactive";
        case Qt::ApplicationActive: return "Active";
        default: return "Unknown";
    }
}


void MusicReader::on_application_state_changed(Qt::ApplicationState state)
{
    if (state == Qt::ApplicationSuspended) {
        // Application is being suspended
        was_suspended = true;
        logger::debug("Application suspended");
        return;
    }

    TRACE_FUNCTION_MSG("state: {} was_suspended: {}", to_string(state), was_suspended);
    if (was_suspended && (state & Qt::ApplicationActive)) {
        // System likely woke from sleep
        QTimer::singleShot(100, this, [this]() {
            force_redraw_all_viewers();
        });
    }
    was_suspended = false;
}


void MusicReader::on_screen_geometry_changed(const QRect& geometry)
{
    Q_UNUSED(geometry);
    TRACE_FUNCTION;
    force_redraw_all_viewers();
}


void MusicReader::on_screen_dpi_changed(qreal dpi)
{
    Q_UNUSED(dpi);
    TRACE_FUNCTION;
    force_redraw_all_viewers();
}


void MusicReader::force_redraw_all_viewers()
{
    TRACE_FUNCTION;
    for (int i = 0; i < tab_widget_->count(); ++i) {
        PDFViewer* viewer = viewer_tab(i);
        if (viewer)
            viewer->force_redraw();
    }
}


void MusicReader::open_tour_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    try {
        TourDialog dialog(this);
        dialog.exec();
    } catch (const std::exception& e) {
        logger::error("Failed to open tour dialog: " + std::string(e.what()));
        display_error_message("Failed to open tour dialog: " + std::string(e.what()));
    }
}


void MusicReader::check_first_run_tour()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!config_.tour_has_run()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Welcome to MusicReader");
        msgBox.setText("Would you like to take a quick tour of MusicReader's features?");
        msgBox.setInformativeText("You can always access the tour later through the menu View | Tour...");
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);

        config_.set_tour_has_run(true); // Mark as shown regardless of choice

        if (msgBox.exec() == QMessageBox::Yes)
            open_tour_dialog();
    }
}


void MusicReader::toggle_page_break_edit_mode()
{
    SAFE_METHOD;

    auto viewer = current_viewer();
    if (!viewer)
        return;

    bool editing = !viewer->in_page_break_edit_mode();

    if (editing && PerformanceMode::is_performance()) {
        QSignalBlocker blocker(performance_mode_action_);
        performance_mode_action_->setChecked(false);
        viewer->set_performance_mode(PerformanceMode::Mode::Normal);
    }

    viewer->set_page_break_edit_mode(editing);
}


void MusicReader::toggle_performance_mode()
{
    SAFE_METHOD;

    auto viewer = current_viewer();
    if (!viewer)
        return;

    PerformanceMode::Mode current_mode = PerformanceMode::get();
    PerformanceMode::Mode new_mode = PerformanceMode::toggled(current_mode);

    if (new_mode == PerformanceMode::Mode::Performance && viewer->in_page_break_edit_mode()) {
        QSignalBlocker blocker(page_break_edit_action_);
        page_break_edit_action_->setChecked(false);
        viewer->set_page_break_edit_mode(false);
    }

    viewer->set_performance_mode(new_mode);
    viewer->refresh();
}
