
#include "music_reader.h"
#include "logger.h"
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <QtWidgets>
#include <QtConcurrent/QtConcurrent>
#include <QJsonDocument>
#include <QJsonObject>

#include "bookmark_titlebar.h"
#include "bookmark_treewidget.h"
#include "document.h"
#include "document_info.h"
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
#include "font_info.h"
#include "poly_metronome_dialog.h"
#include "poly_metronome.h"

constexpr int HIDE_MOUSE_TIMEOUT_MS = 5000;

namespace {
// QMenu subclass that keeps itself open when the user toggles a checkable
// action. Used by the "Open Recent" menu so the user can check multiple items
// before committing via the "Open Selected" action.
class StayOpenMenu : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QAction* action = activeAction();
        if (action && action->isCheckable() && action->isEnabled()) {
            action->trigger();
            return;
        }
        QMenu::mouseReleaseEvent(event);
    }
};
} // namespace

MusicReader::MusicReader(QWidget* parent)
    : QMainWindow(parent)
    , config_(ConfigFile::instance())
    , load_manager_(std::min(80, (int)std::thread::hardware_concurrency()))
    //, load_manager_(1)
{
#if !defined(NDEBUG)
    logger::initialize(true, config_, 1024);
#else
    logger::initialize(true, config_, 1024);
#endif

    update_logging_level();
    setup_UI();
}

MusicReader::~MusicReader() = default;

// DOCUMENT AND TABS
void MusicReader::on_document_loaded(std::string name, int page)
{
    auto i = doc_is_open(name);
    if (i.has_value()) {
        auto viewer = viewer_tab(i.value());
        if (viewer)
            viewer->get_page(page);
    }
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

void MusicReader::on_reload_document()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto viewer = current_viewer();
    auto doc = current_document();
    if (!viewer || !doc)
        return;

    int page_num = viewer->current_page();

    // Ensure any pending changes are saved to disk before reloading
    if (doc->is_modified()) {
        doc->save();
        doc->wait_for_save();
    }

    doc->kill_load();
    load_manager_.remove_document(doc->filename());

    PDFViewer* reloaded_viewer = open_pdf_in_tab(doc->filename(), page_num, viewer);

    // If the PDF was generated externally (say, by musescore), the bookmarks
    // created by this app will be lost. If the document has no bookmarks, but
    // we have saved bookmarks for it in its .mrd info file, restore them.
    if (reloaded_viewer) {
        doc = reloaded_viewer->document();
        if (doc->bookmarks().empty() && doc->bookmarks_file_exists())
            doc->set_bookmarks_from_file();
    }

    // Tab probably didn't change, but this ensures everything gets redrawn - page numbers, bookmarks, etc
    on_tab_current_changed();
}

PDFViewer*
MusicReader::open_pdf_in_tab(const std::filesystem::path& filename, int page, PDFViewer* viewer, bool is_temporary)
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

std::shared_ptr<Document> MusicReader::open_pdf_document(const std::filesystem::path& filename, int page_num)
{
    SAFE_METHOD;
    TRACE_FUNCTION;
    try {

        if (!std::filesystem::exists(filename)) {
            logger::debug(filename.u8string() + u8" doesn't exist");
            return {};
        }

        auto doc = std::make_shared<Document>(filename, config_.dpi(), page_num);
        connect(doc.get(), &Document::bookmarks_loaded, this, [this]() {
            bookmark_panel_->populate();
            update_background();
        });
        load_manager_.add_document(doc);
        return doc;
    } catch (const std::exception& e) {
        logger::error("Error opening document {}: {}", filename.string(), e.what());
        display_error_message("Error opening document " + filename.string() + ": " + std::string(e.what()));
        return {};
    }
}


bool MusicReader::in_single_page_mode() const
{
    return config_.page_view_count() == 1;
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

void MusicReader::restore_open_documents()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    REQUIRES(bookmark_panel_);
    REQUIRES(tab_widget_);

    logger::debug("Restoring open documents...");
    SaveState state(restoring_documents_, true);

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
    logger::trace("first doc in priority order: {}", ordered_docs.empty() ? "none" : ordered_docs[0].string());
    load_manager_.set_document_priority_order(ordered_docs);
}


// SCREEN/DRAWING STUFF
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


// MOUSE, KEYBOARD AND WINDOW EVENTS


void MusicReader::closeEvent(QCloseEvent* event)
{
    SAFE_METHOD;
    TRACE_FUNCTION;


    load_manager_.stop_loading();
    DevStatusDialog::close_if_open();
    flush_metronome_save();
    save_config();

    int num_open_docs = tab_widget_ ? tab_widget_->count() : 0;
    for (int i = 0; i < num_open_docs; ++i) {
        auto viewer = viewer_tab(i);
        if (viewer)
            viewer->closing();
    }


    QMainWindow::closeEvent(event); // Call base class implementation
    check_for_errors_on_exit();
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

    // Persist the metronome dialog's geometry whenever it goes away.
    //
    // QEvent::Hide fires for both an explicit hide() and a close(); since
    // the dialog has WA_DeleteOnClose=false, close() just hides it, so
    // catching Hide covers both paths. The next show_metronome_dialog()
    // call will restore from these saved coordinates.
    //
    // This filter is independent of the dialog's own internal event
    // filter (which lives in PolyMetronomeLib and only swallows
    // FocusIn/WindowActivate to enforce no_focus mode). The two filters
    // never see the same events.
    if (watched == metronome_dialog_ && event->type() == QEvent::Hide) {
        QRect geom = metronome_dialog_->geometry();
        config_.set_metronome_dialog_pos({geom.x(), geom.y(), geom.width(), geom.height()});
    }

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

void MusicReader::save_window_state_to_config()
{
    SAFE_METHOD;
    TRACE_CALL;

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

    auto* viewer = new FileViewer(logger::log_file_path(), nullptr, "[error]");
    viewer->show();
}

void MusicReader::set_bookmarks_from_file()
{
    auto doc = current_document();
    if (!doc)
        return;

    doc->set_bookmarks_from_file();
    update_bookmarks_for_doc();
}

void MusicReader::save_bookmarks_to_file()
{
    auto doc = current_document();
    if (doc)
        doc->save_bookmarks_to_file();
}

void MusicReader::update_recent_files_list()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    open_recent_menu_->clear();

    QAction* open_selected = new QAction("&Open Selected", this);
    open_recent_menu_->addAction(open_selected);
    open_recent_menu_->addSeparator();

    for (const auto& path : std::views::reverse(config_.recent_documents())) {
        QString display_text = QString::fromStdWString(path.wstring());
        QAction* action = new QAction(display_text, this);
        action->setToolTip(display_text);
        action->setCheckable(true);
        action->setData(QVariant::fromValue(QString::fromStdWString(path.wstring())));
        open_recent_menu_->addAction(action);
    }

    connect(open_selected, &QAction::triggered, this, [this]() {
        std::vector<std::filesystem::path> to_open;
        for (QAction* a : open_recent_menu_->actions()) {
            if (a->isCheckable() && a->isChecked())
                to_open.emplace_back(a->data().toString().toStdWString());
        }
        for (const auto& p : to_open)
            open_pdf_in_tab(p);
    });
}

void MusicReader::on_browse_folder()
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

    QString folder_path = QDir::toNativeSeparators(file_info.absolutePath());
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

void MusicReader::on_external_edit_document()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto doc = current_document();
    if (!doc)
        return;

    auto filename = doc->filename();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(filename)));
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


// TAB STUFF
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

void MusicReader::on_tab_current_changed()
{
    SAFE_METHOD;
    auto viewer = current_viewer();
    TRACE_FUNCTION_MSG("{}", viewer ? viewer->document()->filename() : "no tabs");

    // Clear annotation selection when switching tabs
    if (viewer)
        viewer->clear_selection();

    // Persist any pending metronome change to the previous document, then
    // load the new document's state into the dialog (no-op if dialog absent).
    if (metronome_save_pending_doc_)
        flush_metronome_save();
    apply_metronome_state_to_dialog();

    update_title();
    update_bookmark_panel();
    show_page_count();
    if (!viewer)
        return;

    viewer->refresh_if_dirty();
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

void MusicReader::on_tab_moved(int /*from*/, int /*to*/)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    save_open_documents_to_config();
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
    on_tab_current_changed();

    QTimer::singleShot(1000, this, [this] {
        update_memory_usage();
    });
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

void MusicReader::save_config()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    save_open_documents_to_config();
    save_window_state_to_config();
    update_logging_level();
}

void MusicReader::save_open_documents_to_config()
{
    SAFE_METHOD;
    TRACE_CALL;

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

void MusicReader::update_title(int index)
{
    SAFE_METHOD;
    TRACE_CALL;

    index;

    if (tab_widget_->count() > 0) {
        QString current_tab_title = tab_widget_->tabText(tab_widget_->currentIndex());
        setWindowTitle(current_tab_title);
    } else
        setWindowTitle("MusicReader");
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

    // Mark other tabs dirty - they'll refresh when selected
    for (int i = 0; i < tab_widget_->count(); ++i) {
        PDFViewer* viewer = viewer_tab(i);
        if (viewer && viewer != current)
            viewer->mark_dirty();
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
        case LogLevel::DebugAndTrace: logger::enable_debug_and_trace_logging(true); break;
        default: logger::enable_debug_logging(false); break;
    }
}

void MusicReader::restore_window_state()
{
    SAFE_METHOD;
    TRACE_CALL;

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
    TRACE_CALL;

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

void MusicReader::on_toolbar_page_changed(int index_0_based)
{
    PDFViewer* viewer = current_viewer();
    if (!viewer)
        return;

    viewer->get_page(index_0_based + 1);
}

// notification from PDFViewer when page changes
void MusicReader::on_viewer_page_changed(int /*page_num*/)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    show_page_count();
}

void MusicReader::update_background()
{
    SAFE_METHOD;
    TRACE_CALL;

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
    menuBar()->clear();
    create_menus();
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
    update_music_palette_visibility(enabled);
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

    update_music_palette_visibility(text_annotation_mode_);
}


// SMuFL codepoints (Bravura). Listed roughly in order of expected usage.
static const struct {
    int codepoint;
    const char* tooltip;
} k_music_palette_glyphs[] = {
    {0xE262, "Sharp"},
    {0xE260, "Flat"},
    {0xE261, "Natural"},
    {0xE263, "Double Sharp"},
    {0xE264, "Double Flat"},
};


// Render a single Bravura glyph into a QIcon. We rely on Qt finding the system
// "Bravura" font (the same lookup the C++ side uses via lookup_font_file).
static QIcon make_glyph_icon(int codepoint, int pixel_size)
{
    QPixmap pix(pixel_size, pixel_size);
    pix.fill(Qt::transparent);

    QFont font("Bravura");
    font.setPixelSize(pixel_size);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    p.setPen(QApplication::palette().color(QPalette::ButtonText));

    // SMuFL Private-Use codepoints (U+E000–U+F8FF) all fit in a single QChar.
    QString glyph(QChar(static_cast<char16_t>(codepoint)));
    p.drawText(pix.rect(), Qt::AlignCenter, glyph);

    return QIcon(pix);
}


void MusicReader::create_music_palette()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    music_palette_group_ = new QActionGroup(this);
    music_palette_group_->setExclusive(true);

    const int icon_px = toolbar_->iconSize().height();
    for (const auto& g : k_music_palette_glyphs) {
        QAction* action = new QAction(make_glyph_icon(g.codepoint, icon_px), "", this);
        action->setToolTip(QString("%1\nRight-click for font settings").arg(g.tooltip));
        action->setCheckable(true);
        action->setData(g.codepoint);
        music_palette_group_->addAction(action);
        toolbar_->addAction(action);
        action->setVisible(false);
        connect(action, &QAction::toggled, this, &MusicReader::on_music_symbol_action_toggled);
        music_palette_actions_.push_back(action);

        if (QWidget* button = toolbar_->widgetForAction(action)) {
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
                select_annotation_font();
            });
        }
    }
}


void MusicReader::update_music_palette_visibility(bool annotation_mode_on)
{
    for (QAction* a : music_palette_actions_)
        a->setVisible(annotation_mode_on);

    if (!annotation_mode_on) {
        // Leaving annotation mode clears any selected palette glyph and the
        // viewer's pending state. setChecked emits toggled(false) which routes
        // through on_music_symbol_action_toggled and clears the viewer.
        for (QAction* a : music_palette_actions_)
            if (a->isChecked())
                a->setChecked(false);
    }
}


void MusicReader::on_music_symbol_action_toggled()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto viewer = current_viewer();
    if (!viewer)
        return;

    QAction* checked = music_palette_group_->checkedAction();
    int codepoint = checked ? checked->data().toInt() : 0;
    viewer->set_pending_music_symbol(codepoint);
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

void MusicReader::force_redraw_all_viewers()
{
    TRACE_FUNCTION;
    for (int i = 0; i < tab_widget_->count(); ++i) {
        PDFViewer* viewer = viewer_tab(i);
        if (viewer)
            viewer->force_redraw();
    }
}

void MusicReader::show_titlebar_menu()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    QMenu menu(this);

    create_file_menu(&menu);
    create_edit_menu(&menu);
    create_imslp_menu(&menu);
    create_view_menu(&menu);
    create_help_menu(&menu);


    // Separator before Exit
    menu.addSeparator();

    // Standalone Exit Action
    QAction* exit_action = new QAction("E&xit", this);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);
    menu.addAction(exit_action);

    menu.exec(QCursor::pos());
}

void MusicReader::check_first_run_tour()
{
    SAFE_METHOD;
    TRACE_CALL;

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


// DIALOGS

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

void MusicReader::show_keyboard_shortcuts()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    struct Entry { QString key; QString action; };
    struct Section { QString name; std::vector<Entry> entries; };

    const std::vector<Section> sections = {
        { "Navigation", {
            { "Page Down / Down / Space / Right", "Next page" },
            { "Page Up / Up / Left",              "Previous page" },
            { "G",                                "Go to page..." },
            { "F11",                              "Toggle fullscreen" },
            { "Escape",                           "Exit fullscreen" },
        }},
        { "View", {
            { "1",       "Single page view" },
            { "2",       "Double page view" },
            { "Z",       "Toggle zoom to content" },
            { "S",       "Toggle page step size" },
            { "T",       "Show / hide document tabs" },
            { "Ctrl+B",  "Show / hide bookmark panel" },
        }},
        { "File", {
            { "O",   "Open file" },
            { "F",   "Fast file search" },
            { "I",   "IMSLP search" },
            { "F5",  "Reload document" },
            { "F2",  "Edit document (external)" },
        }},
        { "Bookmarks & Annotations", {
            { "B / Ctrl+D",          "Add bookmark" },
            { "Ctrl+Z",              "Undo" },
            { "Ctrl+Y",              "Redo" },
            { "A",                   "Toggle annotation mode" },
            { "Delete / Backspace",  "Delete selected annotation" },
            { "Arrow keys",          "Move selected annotation" },
            { "+  /  -",             "Resize annotation font" },
        }},
        { "Modes", {
            { "P",   "Toggle performance mode" },
            { "M",   "Toggle metronome start / stop" },
            { ",",   "Metronome BPM − 1" },
            { ".",   "Metronome BPM + 1" },
        }},
        { "Page Break Edit Mode", {
            { "X", "Toggle paper-crop submode (top / bottom)" },
            { "Z", "Toggle paper-crop submode (left / right)" },
        }},
    };

    // Count total rows: one header row per section + entries
    int total_rows = 0;
    for (auto& s : sections)
        total_rows += 1 + static_cast<int>(s.entries.size());

    QDialog dialog(this);
    dialog.setWindowTitle("Keyboard Shortcuts");
    dialog.setModal(true);
    dialog.resize(520, 560);

    QVBoxLayout layout(&dialog);

    auto* table = new QTableWidget(total_rows, 2, &dialog);
    table->setHorizontalHeaderLabels({ "Key", "Action" });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setShowGrid(false);
    table->setFocusPolicy(Qt::NoFocus);

    // Section header colour — a slightly lighter shade of the window background
    const QColor header_bg = palette().color(QPalette::Mid);
    const QColor header_fg = palette().color(QPalette::BrightText);
    QFont header_font;
    header_font.setBold(true);

    int row = 0;
    for (auto& section : sections) {
        // Section header spans both columns
        auto* header_item = new QTableWidgetItem(section.name);
        header_item->setBackground(header_bg);
        header_item->setForeground(header_fg);
        header_item->setFont(header_font);
        header_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table->setItem(row, 0, header_item);
        table->setItem(row, 1, new QTableWidgetItem());
        table->item(row, 1)->setBackground(header_bg);
        table->setSpan(row, 0, 1, 2);
        table->setRowHeight(row, 24);
        ++row;

        for (auto& e : section.entries) {
            table->setItem(row, 0, new QTableWidgetItem(e.key));
            table->setItem(row, 1, new QTableWidgetItem(e.action));
            table->setRowHeight(row, 22);
            ++row;
        }
    }

    layout.addWidget(table);

    QHBoxLayout button_layout;
    auto* ok_button = new QPushButton("OK", &dialog);
    connect(ok_button, &QPushButton::clicked, &dialog, &QDialog::accept);
    button_layout.addStretch();
    button_layout.addWidget(ok_button);
    layout.addLayout(&button_layout);

    dialog.exec();
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

std::optional<FontInfo> MusicReader::show_font_picker(QWidget* parent, const FontInfo& current)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Select Annotation Font");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* family_label = new QLabel("Font Family:", &dialog);
    layout->addWidget(family_label);

    QComboBox* font_combo = new QComboBox(&dialog);
    font_combo->addItem("Courier");
    font_combo->addItem("Helvetica");
    font_combo->addItem("Times");
    font_combo->addItem("Symbol");
    // Bravura isn't user-pickable for new text but appears here when an
    // existing music-symbol annotation is being edited.
    if (current.family == "Bravura")
        font_combo->addItem("Bravura");

    int current_index = font_combo->findText(QString::fromStdString(current.family));
    if (current_index >= 0)
        font_combo->setCurrentIndex(current_index);
    layout->addWidget(font_combo);

    QLabel* size_label = new QLabel("Font Size:", &dialog);
    layout->addWidget(size_label);

    QComboBox* size_combo = new QComboBox(&dialog);
    size_combo->setEditable(true);
    for (int i = 0; i < k_standard_font_sizes_count; ++i)
        size_combo->addItem(QString::number(static_cast<int>(k_standard_font_sizes[i])));
    size_combo->setCurrentText(QString::number(static_cast<int>(current.size)));
    layout->addWidget(size_combo);

    QLabel* color_label = new QLabel("Font Color:", &dialog);
    layout->addWidget(color_label);

    auto [r, g, b] = current.color;
    QColor selected_color(r, g, b);

    QPushButton* color_button = new QPushButton(&dialog);
    color_button->setMinimumHeight(30);
    auto update_color_button = [color_button](const QColor& color) {
        color_button->setStyleSheet(QString("background-color: %1; border: 1px solid gray;").arg(color.name()));
        color_button->setText(color.name());
    };
    update_color_button(selected_color);

    QObject::connect(color_button, &QPushButton::clicked, [&dialog, &selected_color, update_color_button]() {
        QColor new_color = QColorDialog::getColor(selected_color, &dialog, "Select Font Color");
        if (new_color.isValid()) {
            selected_color = new_color;
            update_color_button(new_color);
        }
    });
    layout->addWidget(color_button);

    QDialogButtonBox* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(button_box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(button_box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(button_box);

    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;

    FontInfo result;
    result.family = font_combo->currentText().toStdString();
    bool ok = false;
    float typed = size_combo->currentText().toFloat(&ok);
    result.size = ok ? typed : current.size;
    result.color = {selected_color.red(), selected_color.green(), selected_color.blue()};
    return result;
}


void MusicReader::select_annotation_font()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto picked = show_font_picker(this, config_.annotation_font());
    if (!picked)
        return;

    config_.set_annotation_font(*picked);
    auto [pr, pg, pb] = picked->color;
    logger::info("Annotation font set to: family='{}', size={}, color=({},{},{})", picked->family,
                 picked->size, pr, pg, pb);
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

void MusicReader::open_dev_status_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    DevStatusDialog::show(config_, this);
}

// Lazy-create the metronome dialog on first call, then restore its last
// known geometry and per-PDF state and bring it on screen.
//
// The dialog is constructed with no_focus=true: PolyMetronomeLib then sets
// it up as a frameless, never-activating tool palette so MusicReader keeps
// keyboard focus while the user clicks sliders/dials/buttons inside it.
// See PolyMetronomeDialog's constructor for the focus-prevention details.
//
// The dialog is reused across opens — WA_DeleteOnClose is disabled so
// hide() leaves it intact; subsequent calls just re-show the existing
// instance. The QObject::destroyed connection clears the pointer in the
// event Qt does eventually tear it down (e.g. on app shutdown).
void MusicReader::show_metronome_dialog()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (!metronome_dialog_) {
        // First-time construction: build the dialog and wire up everything
        // that must persist for the dialog's lifetime.
        metronome_dialog_ = new PolyMetronomeDialog(this, /*no_focus=*/true);
        metronome_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
        connect(metronome_dialog_, &QObject::destroyed, this, [this]() { metronome_dialog_ = nullptr; });

        // state_changed fires whenever the user moves a slider, spins the
        // dial, edits a meter, etc. We use it to write per-PDF state.
        connect(metronome_dialog_, &PolyMetronomeDialog::state_changed, this,
                &MusicReader::on_metronome_state_changed);

        // 500ms debounce: collapses bursts of state_changed signals (slider
        // drags) into a single info file write.
        metronome_save_timer_ = new QTimer(this);
        metronome_save_timer_->setSingleShot(true);
        metronome_save_timer_->setInterval(500);
        connect(metronome_save_timer_, &QTimer::timeout, this, &MusicReader::flush_metronome_save);

        // Watch for QEvent::Hide so we can persist the dialog's last
        // on-screen geometry to the global config (see eventFilter()).
        // This is independent of the focus-blocking event filter the
        // dialog installs on itself in no_focus mode — that one only
        // watches FocusIn/WindowActivate and lives in the library.
        metronome_dialog_->installEventFilter(this);
    }

    // Pull the active document's saved metronome settings into the dialog.
    apply_metronome_state_to_dialog();

    // Restore the last on-screen size and position from the global config.
    // Width/height are applied first so the dialog is the right size when
    // we test whether the saved top-left corner is on a connected screen.
    const auto& pos = config_.metronome_dialog_pos();
    QScreen* target_screen = nullptr;
    if (pos[0] >= 0 && pos[1] >= 0)
        target_screen = QGuiApplication::screenAt(QPoint(pos[0], pos[1]));
    if (!target_screen)
        target_screen = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!target_screen)
        target_screen = QGuiApplication::primaryScreen();

    if (target_screen) {
        // Clamp restored width/height to the target screen so an over-sized
        // saved rect from a previous (larger) resolution can't push the
        // dialog off the new desktop.
        QRect avail = target_screen->availableGeometry();
        if (pos[2] > 0 && pos[3] > 0) {
            int w = std::min(pos[2], avail.width());
            int h = std::min(pos[3], avail.height());
            metronome_dialog_->resize(w, h);
        }
        if (pos[0] >= 0 && pos[1] >= 0 && QGuiApplication::screenAt(QPoint(pos[0], pos[1]))) {
            // Saved top-left still lands on a connected screen. Nudge inward
            // if the (possibly clamped) size would now extend past the edge.
            QSize sz = metronome_dialog_->size();
            int x = std::min(pos[0], avail.right() - sz.width() + 1);
            int y = std::min(pos[1], avail.bottom() - sz.height() + 1);
            metronome_dialog_->move(x, y);
        } else {
            // No saved position, or the monitor it was on is gone. Centre on
            // whichever screen the main window lives on so the dialog can't
            // end up stranded off-screen.
            metronome_dialog_->move(avail.center() - metronome_dialog_->rect().center());
        }
    } else if (pos[2] > 0 && pos[3] > 0) {
        // No screen info at all — apply the saved size as-is and let Qt place it.
        metronome_dialog_->resize(pos[2], pos[3]);
    }

    // show() + raise() — deliberately no activateWindow() because that would
    // contradict the dialog's WA_ShowWithoutActivating attribute and pull
    // focus away from MusicReader. The dialog must never become the active
    // window; its WS_EX_NOACTIVATE style enforces that on Windows.
    metronome_dialog_->show();
    metronome_dialog_->raise();
}

// Per-PDF metronome state: load
//
// Reads the active document's metronome state (a compact JSON blob stored
// in the .mrd info file) and pushes it into the dialog. apply_state() does
// not re-emit state_changed, so this round-trip won't kick the save
// timer back on.
//
// Falls through to a default-constructed PolyMetronomeState (nice
// defaults built into the struct) if there is no document, no saved
// state, or the JSON fails to parse.
//
// Currently early-returned — the per-PDF feature is paused while the
// approach is reworked. The body is kept so the wiring is preserved
// for when it's re-enabled. See also on_metronome_state_changed and
// flush_metronome_save.
void MusicReader::apply_metronome_state_to_dialog()
{
    SAFE_METHOD;
    return; // TODO: per-PDF metronome state temporarily disabled
    /*if (!metronome_dialog_)
        return;

    PolyMetronomeState s;
    auto doc = current_document();
    if (doc) {
        const std::string& json_str = doc->performance_data().metronome_state();
        if (!json_str.empty()) {
            QJsonParseError err{};
            QJsonDocument jd = QJsonDocument::fromJson(QByteArray::fromStdString(json_str), &err);
            if (err.error == QJsonParseError::NoError && jd.isObject())
                s = PolyMetronomeState::from_json(jd.object());
            else
                logger::warning("Metronome state JSON parse failed for {}: {}", doc->filename(),
                                err.errorString().toStdString());
        }
    }
    metronome_dialog_->apply_state(s);*/
}

// Per-PDF metronome state: capture pending save
//
// Connected to PolyMetronomeDialog::state_changed. Serialises the
// dialog's current state to compact JSON, stashes it in the active
// document's PerformanceData, and (re)starts the debounce timer so the
// info file is only written once after a burst of changes settles.
//
// If a save is already pending for a *different* document (the user
// switched tabs mid-edit), flush that one first so its edits are not
// clobbered when we overwrite metronome_save_pending_doc_ below.
//
// Currently early-returned — see apply_metronome_state_to_dialog.
void MusicReader::on_metronome_state_changed()
{
    SAFE_METHOD;
    return; // TODO: per-PDF metronome state temporarily disabled
    /*if (!metronome_dialog_)
        return;
    auto doc = current_document();
    if (!doc)
        return;

    if (metronome_save_pending_doc_ && metronome_save_pending_doc_ != doc)
        flush_metronome_save();

    QJsonDocument jd(metronome_dialog_->state().to_json());
    QByteArray ba = jd.toJson(QJsonDocument::Compact);
    doc->performance_data().set_metronome_state(ba.toStdString());

    metronome_save_pending_doc_ = doc;
    metronome_save_timer_->start();*/
}

// Per-PDF metronome state: write to disk
//
// Flushes any pending state change to the corresponding document's
// .mrd info file. Called from the debounce timer's timeout, on tab change,
// and at app exit. Cancels the timer first so a flush forced from
// outside the timer doesn't re-fire it for the same data.
//
// Currently early-returned — see apply_metronome_state_to_dialog.
void MusicReader::flush_metronome_save()
{
    SAFE_METHOD;
    return; // TODO: per-PDF metronome state temporarily disabled
    /*if (metronome_save_timer_)
        metronome_save_timer_->stop();
    if (!metronome_save_pending_doc_)
        return;
    metronome_save_pending_doc_->save_performance_data();
    metronome_save_pending_doc_.reset();*/
}

void MusicReader::save_current_page_as_bmp()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto doc = current_document();
    if (!doc) {
        display_error_message("No document open");
        return;
    }

    auto [page_num, valid] = current_page();
    if (!valid) {
        display_error_message("Could not determine current page");
        return;
    }

    Page page = doc->get_page(page_num, true);
    if (page.is_empty()) {
        display_error_message("Page image is empty");
        return;
    }

    std::filesystem::path doc_path = doc->path();
    std::filesystem::path dir = doc_path.parent_path();
    std::string stem = doc_path.stem().string();
    std::string filename = stem + "_" + std::to_string(page_num) + ".bmp";
    std::filesystem::path output_path = dir / filename;

    // Convert to grayscale if the source is grayscale to preserve format
    QImage img_to_save = page.img;
    if (page.img.format() == QImage::Format_Grayscale8 || page.img.format() == QImage::Format_Grayscale16) {
        img_to_save = page.img.convertToFormat(QImage::Format_Grayscale8);
    }

    QString qpath = QString::fromStdWString(output_path.wstring());
    if (img_to_save.save(qpath, "BMP")) {
        logger::info("Saved page {} to {} (format: {})", page_num, output_path.string(),
                     static_cast<int>(page.img.format()));
    } else {
        display_error_message("Failed to save image to " + output_path.string());
    }
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

void MusicReader::initialize_fast_search()
{
    SAFE_METHOD;
    TRACE_CALL;

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

// UI Setup

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
    connect(tab_widget_, &QTabWidget::currentChanged, this, &MusicReader::on_tab_current_changed);
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

void MusicReader::create_file_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_CALL;

    // File menu
    QMenu* file_menu = menu_bar->addMenu("&File");
    QAction* action;

    action = new QAction("&Fast Search...", this);
    connect(action, &QAction::triggered, this, &MusicReader::open_fast_search_dialog);
    file_menu->addAction(action);

    action = new QAction("&Open...", this);
    file_menu->addAction(action);
    connect(action, &QAction::triggered, this, &MusicReader::open_file_dialog_default_path);


    open_recent_menu_ = new StayOpenMenu("Open &Recent", this);
    file_menu->addMenu(open_recent_menu_);
    connect(open_recent_menu_, &QMenu::aboutToShow, this, &MusicReader::update_recent_files_list);

    file_menu->addSeparator();

    action = new QAction("&Settings...", this);
    connect(action, &QAction::triggered, this, &MusicReader::open_config_dialog);
    file_menu->addAction(action);

    QAction* exit_action = new QAction("E&xit", this);
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);
    file_menu->addAction(exit_action);
}

void MusicReader::create_edit_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_CALL;

    edit_menu_ = menu_bar->addMenu("&Edit");

    if (!undo_action_) {
        undo_action_ = new QAction("Undo", this);
        undo_action_->setShortcut(QKeySequence::Undo);
        connect(undo_action_, &QAction::triggered, bookmark_panel_, &BookmarkPanel::undo);
        addAction(undo_action_);
    }
    undo_action_->setDisabled(true);
    edit_menu_->addAction(undo_action_);

    if (!redo_action_) {
        redo_action_ = new QAction("Redo", this);
        redo_action_->setShortcut(QKeySequence::Redo);
        connect(redo_action_, &QAction::triggered, bookmark_panel_, &BookmarkPanel::redo);
        addAction(redo_action_);
    }
    redo_action_->setEnabled(false);
    edit_menu_->addAction(redo_action_);

    edit_menu_->addSeparator();

    QAction* set_bookmarks_action = new QAction("Set bookmarks from file", this);
    connect(set_bookmarks_action, &QAction::triggered, this, &MusicReader::set_bookmarks_from_file);
    edit_menu_->addAction(set_bookmarks_action);
    QAction* save_bookmarks_action = new QAction("Save bookmarks to file", this);
    connect(save_bookmarks_action, &QAction::triggered, this, &MusicReader::save_bookmarks_to_file);
    edit_menu_->addAction(save_bookmarks_action);

    edit_menu_->addSeparator();

    QAction* select_font_action = new QAction("Select Annotation &Font...", this);
    connect(select_font_action, &QAction::triggered, this, &MusicReader::select_annotation_font);
    edit_menu_->addAction(select_font_action);

    edit_menu_->addSeparator();

    if (text_annotation_action_) {
        text_annotation_action_->setText("Text &Annotation Mode");
        edit_menu_->addAction(text_annotation_action_);
    }

    if (config_.in_dev_mode()) {
        edit_menu_->addSeparator();
        auto* save_image_action = new QAction("Save Image", this);
        connect(save_image_action, &QAction::triggered, this, &MusicReader::save_current_page_as_bmp);
        edit_menu_->addAction(save_image_action);
    }

    connect(edit_menu_, &QMenu::aboutToShow, this, [this, set_bookmarks_action, save_bookmarks_action]() {
        auto doc = current_document().get();
        undo_action_->setEnabled(doc && doc->can_undo());
        redo_action_->setEnabled(doc && doc->can_redo());

        // keep it safe, only enable if the action is possible
        set_bookmarks_action->setEnabled(doc && doc->bookmarks_file_exists());
        save_bookmarks_action->setEnabled(doc && doc->bookmarks().size() > 0);
    });
}

void MusicReader::create_view_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_CALL;

    QMenu* view_menu = menu_bar->addMenu("&View");

    if (!bookmark_menu_action_) {
        bookmark_menu_action_ = new QAction("Show Bookmarks", this);
        bookmark_menu_action_->setShortcut(QKeySequence("Ctrl+B"));
        bookmark_menu_action_->setShortcutContext(Qt::ApplicationShortcut);
        bookmark_menu_action_->setCheckable(true);
        connect(bookmark_menu_action_, &QAction::triggered, this, &MusicReader::toggle_bookmark_panel);
    }
    bookmark_menu_action_->setChecked(true);
    view_menu->addAction(bookmark_menu_action_);

    if (!tabs_menu_action_) {
        tabs_menu_action_ = new QAction("Show Document &Tabs", this);
        tabs_menu_action_->setShortcut(Qt::Key_T);
        tabs_menu_action_->setShortcutContext(Qt::ApplicationShortcut);
        tabs_menu_action_->setCheckable(true);
        connect(tabs_menu_action_, &QAction::triggered, this, &MusicReader::toggle_tab_visibility);
        addAction(tabs_menu_action_);
    }
    tabs_menu_action_->setChecked(tabs_visible_);
    view_menu->addAction(tabs_menu_action_);

    if (!menubar_menu_action_) {
        menubar_menu_action_ = new QAction("&Menu Bar", this);
        menubar_menu_action_->setCheckable(true);
        connect(menubar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_menu_visibility);
    }
    menubar_menu_action_->setChecked(config_.show_menu());
    view_menu->addAction(menubar_menu_action_);

    if (!toolbar_menu_action_) {
        toolbar_menu_action_ = new QAction("Tool Bar", this);
        toolbar_menu_action_->setCheckable(true);
        connect(toolbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_toolbar_visibility);
    }
    toolbar_menu_action_->setChecked(true);
    view_menu->addAction(toolbar_menu_action_);

    if (!statusbar_menu_action_) {
        statusbar_menu_action_ = new QAction("&Status Bar", this);
        statusbar_menu_action_->setCheckable(true);
        connect(statusbar_menu_action_, &QAction::triggered, this, &MusicReader::toggle_statusbar_visibility);
    }
    statusbar_menu_action_->setChecked(config_.show_status_bar());
    view_menu->addAction(statusbar_menu_action_);

    if (config_.in_dev_mode()) {
        view_menu->addSeparator();
        auto* dev_action = new QAction("Show &Developer Status Dialog...", this);
        connect(dev_action, &QAction::triggered, this, &MusicReader::open_dev_status_dialog);
        view_menu->addAction(dev_action);
    }

    view_menu->addSeparator();
    auto* action = new QAction("View Log...", this);
    connect(action, &QAction::triggered, this, &MusicReader::show_log_file);
    view_menu->addAction(action);

    action = new QAction("Copy log to clipboard", this);
    connect(action, &QAction::triggered, this, &MusicReader::copy_log_to_clipboard);
    view_menu->addAction(action);

    view_menu->addSeparator();

    if (!goto_action_) {
        goto_action_ = new QAction("&Goto Page...", this);
        goto_action_->setShortcut(QKeySequence(Qt::Key_G));
        connect(goto_action_, &QAction::triggered, this, &MusicReader::goto_page_dialog);
        addAction(goto_action_);
    }
    view_menu->addAction(goto_action_);

    connect(view_menu, &QMenu::aboutToShow, this, [this]() {
        auto doc = current_document().get();
        goto_action_->setEnabled(doc);
    });

    view_menu->addSeparator();
    QAction* metronome_action = new QAction("&Metronome...", this);
    connect(metronome_action, &QAction::triggered, this, &MusicReader::show_metronome_dialog);
    view_menu->addAction(metronome_action);
}

void MusicReader::create_imslp_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_CALL;

    if (!imslp_action_) {
        imslp_action_ = new QAction("&Search...", this);
        imslp_action_->setShortcut(QKeySequence("I"));
        connect(imslp_action_, &QAction::triggered, this, &MusicReader::open_imslp_search_dialog);
        addAction(imslp_action_);
    }

    if (config_.show_menu()) {
        QMenu* imslp_menu = menu_bar->addMenu("&IMSLP");
        imslp_menu->addAction(imslp_action_);
    } else {
        imslp_action_->setText("&IMSLP Search");
        menu_bar->addAction(imslp_action_);
    }
}

void MusicReader::create_help_menu(auto* menu_bar)
{
    SAFE_METHOD;
    TRACE_CALL;

    QMenu* help_menu = menu_bar->addMenu("&Help");

    QAction* shortcuts_action = new QAction("&Keyboard Shortcuts...", this);
    connect(shortcuts_action, &QAction::triggered, this, &MusicReader::show_keyboard_shortcuts);
    help_menu->addAction(shortcuts_action);

    QAction* tour_action = new QAction("&Tour...", this);
    connect(tour_action, &QAction::triggered, this, &MusicReader::open_tour_dialog);
    help_menu->addAction(tour_action);

    help_menu->addSeparator();

    QAction* about_action = new QAction("&About MusicReader...", this);
    connect(about_action, &QAction::triggered, this, &MusicReader::show_about_dialog);
    help_menu->addAction(about_action);
}

void MusicReader::show_context_menu(const QPoint& pos)
{
    SAFE_METHOD;
    TRACE_CALL;

    auto viewer = current_viewer();
    if (!viewer)
        return;
    if (viewer->in_page_break_edit_mode())
        return; // don't show menu in this mode, right-click is used for deleting

    QMenu context_menu(this);

    QAction* reload_action = new QAction("Reload", this);
    reload_action->setShortcut(QKeySequence("F5"));
    connect(reload_action, &QAction::triggered, this, &MusicReader::on_reload_document);

    QAction* edit_action = new QAction("Edit...", this);
    edit_action->setShortcut(QKeySequence("F2"));
    connect(edit_action, &QAction::triggered, this, &MusicReader::on_external_edit_document);

    QAction* open_folder_action = new QAction("Open from containing folder...", this);
    connect(open_folder_action, &QAction::triggered, this, &MusicReader::open_file_dialog_default_path);

    QAction* browse_folder_action = new QAction("Browse containing folder...", this);
    connect(browse_folder_action, &QAction::triggered, this, &MusicReader::on_browse_folder);

    context_menu.addAction(reload_action);
    context_menu.addSeparator();
    context_menu.addAction(edit_action);
    context_menu.addAction(open_folder_action);
    context_menu.addAction(browse_folder_action);

    // Offer to favorite the document only when it is not already a favorite.
    auto doc = viewer->document();
    if (!document_info::is_favorite(doc->path())) {
        QAction* favorite_action = new QAction("Add to Favorites", this);
        connect(favorite_action, &QAction::triggered, this, [doc]() {
            document_info::set_favorite(doc->path(), true);
        });
        context_menu.addSeparator();
        context_menu.addAction(favorite_action);
    }

    context_menu.exec(tab_widget_->mapToGlobal(pos));
}

void MusicReader::create_global_shortcuts()
{
    // Single-key shortcuts should not fire when typing in text fields
    auto is_text_input_focused = []() {
        QWidget* focus_widget = QApplication::focusWidget();
        return focus_widget && (qobject_cast<QLineEdit*>(focus_widget) || qobject_cast<QTextEdit*>(focus_widget));
    };

    QShortcut* shortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::add_bookmark);

    shortcut = new QShortcut(QKeySequence("PageUp"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::on_page_up);

    shortcut = new QShortcut(Qt::Key_B, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        add_bookmark();
    });

    shortcut = new QShortcut(Qt::Key_1, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        set_page_view_count(1);
    });

    shortcut = new QShortcut(Qt::Key_2, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        set_page_view_count(2);
    });

    shortcut = new QShortcut(Qt::Key_S, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        toggle_page_step();
    });

    shortcut = new QShortcut(Qt::Key_O, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        open_file_dialog_default_path();
    });

    shortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        open_fast_search_dialog();
    });

    shortcut = new QShortcut(Qt::Key_Z, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        auto viewer = current_viewer();
        if (viewer && viewer->in_page_break_edit_mode()) {
            viewer->set_page_break_sub_mode(viewer->page_break_sub_mode() == PageBreakSubMode::EditPaperCropLR
                                                ? PageBreakSubMode::EditBreaks
                                                : PageBreakSubMode::EditPaperCropLR);
            return;
        }
        toggle_page_zoom();
    });

    shortcut = new QShortcut(Qt::Key_P, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        toggle_performance_mode();
    });

    shortcut = new QShortcut(Qt::Key_A, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        toggle_text_annotation_mode();
    });

    shortcut = new QShortcut(Qt::Key_X, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        auto viewer = current_viewer();
        if (!viewer || !viewer->in_page_break_edit_mode())
            return;
        viewer->set_page_break_sub_mode(viewer->page_break_sub_mode() == PageBreakSubMode::EditPaperCrop
                                            ? PageBreakSubMode::EditBreaks
                                            : PageBreakSubMode::EditPaperCrop);
    });

    shortcut = new QShortcut(Qt::Key_Space, this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        on_page_down();
    });

    // Metronome shortcuts. M opens the dialog if it isn't up yet, otherwise
    // toggles play/stop. Comma/period nudge BPM down/up by 1, calling the
    // dialog's set_bpm() (the same path the +/- buttons use) so the current
    // beat position in the measure is preserved — apply_state() can't be
    // used here because it resets the sequence playback.
    shortcut = new QShortcut(Qt::Key_M, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        if (!metronome_dialog_)
            show_metronome_dialog();
        else
            metronome_dialog_->toggle_start_stop();
    });

    shortcut = new QShortcut(Qt::Key_Comma, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        // No-op when dialog hasn't been opened — there's nothing to slow down.
        if (metronome_dialog_)
            metronome_dialog_->set_bpm(metronome_dialog_->state().bpm - 1);
    });

    shortcut = new QShortcut(Qt::Key_Period, this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, is_text_input_focused]() {
        if (is_text_input_focused())
            return;
        if (metronome_dialog_)
            metronome_dialog_->set_bpm(metronome_dialog_->state().bpm + 1);
    });

    shortcut = new QShortcut(QKeySequence("F5"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::on_reload_document);

    shortcut = new QShortcut(QKeySequence("F2"), this);
    shortcut->setContext(Qt::ApplicationShortcut);
    connect(shortcut, &QShortcut::activated, this, &MusicReader::on_external_edit_document);
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

    text_annotation_action_ = new QAction(QIcon(":/MusicReader/images/annotation.ico"), "", this);
    text_annotation_action_->setToolTip("Text Annotation Mode (A)\nRight-click for font settings");
    text_annotation_action_->setCheckable(true);
    // text_annotation_action_->setEnabled(config_.debug_annotations());
    connect(text_annotation_action_, &QAction::triggered, this, &MusicReader::toggle_text_annotation_mode);
    toolbar_->addAction(text_annotation_action_);

    // Add right-click context menu to annotation button for font settings
    if (QWidget* annotation_button = toolbar_->widgetForAction(text_annotation_action_)) {
        annotation_button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(annotation_button, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
            select_annotation_font();
        });
    }

    create_music_palette();

    {
        auto* action = new QAction(QIcon(QPixmap(":/MusicReader/images/gear.png")), "", this);
        action->setToolTip("Settings");
        connect(action, &QAction::triggered, this, &MusicReader::open_config_dialog);
        toolbar_->addAction(action);
    }

    {
        auto* action = new QAction(QIcon(":/MusicReader/images/metronome.svg"), "", this);
        action->setToolTip("Metronome");
        connect(action, &QAction::triggered, this, &MusicReader::show_metronome_dialog);
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

void MusicReader::create_bookmark_panel()
{
    SAFE_METHOD;
    TRACE_CALL;

    bookmark_panel_ = new BookmarkPanel(this);
    [[maybe_unused]] bool s =
        connect(bookmark_panel_, &BookmarkPanel::bookmark_clicked, this, &MusicReader::go_to_bookmark);

    connect(bookmark_panel_, &BookmarkPanel::bookmark_visibility_changed, this,
            &MusicReader::update_menu_bookmark_visibility);
}

void MusicReader::create_status_bar()
{
    SAFE_METHOD;
    TRACE_CALL;

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

QIcon MusicReader::create_double_icon()
{
    SAFE_METHOD;
    TRACE_CALL;

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
