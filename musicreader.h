#pragma once

#include <QtWidgets>
#include <memory>
#include <optional>
#include <filesystem>

#include "config_file.h"
#include "document_load_manager.h"

class BookmarkPanel;
class Document;
class PDFViewer;
class StatusBar;
class FileViewer;
class FastFileSearchDialog;
class FullscreenExitButton;

class MusicReader : public QMainWindow {
    Q_OBJECT

public:
    MusicReader(QWidget *parent = nullptr);

    std::shared_ptr<Document> current_document(const std::string &log_msg = "") const;
    std::pair<int, bool> current_page(const std::string &log_msg = "") const;


    PDFViewer *open_pdf_in_tab(const std::filesystem::path &filename, int page = 1, PDFViewer *tab_to_use = nullptr, bool is_temporary = false);
    std::shared_ptr<Document> open_pdf_document(const std::filesystem::path &filename, int page_num);

    void display_error_message(const std::string &msg);
    bool display_query(const std::string &msg);

    ConfigFile &config() { return config_; }
    const ConfigFile &config() const { return config_; }

public slots:
    void show_titlebar_menu();
    void update_bookmarks_for_doc();
    void browse_folder();
    void on_tab_changed();
    void copy_log_to_clipboard();
    void goto_page_dialog();
    void on_annotation_mode_changed(bool enabled);
    void on_tab_moved(int from, int to);

private slots:
    void on_application_state_changed(Qt::ApplicationState state);
    void on_screen_geometry_changed(const QRect &geometry);
    void on_screen_dpi_changed(qreal dpi);
    void force_redraw_all_viewers();
    void on_toolbar_page_changed(int page);
    void on_viewer_page_changed(int page_num);

private:
    std::shared_ptr<Document> document_at(int index) const;
    PDFViewer *current_tab() const;
    std::string current_document_name() const;
    PDFViewer *viewer_tab(int index) const;
    PDFViewer *current_viewer(const std::string &log_err = "") const;

    void create_help_menu(auto *menu_bar);
    void show_about_dialog();

    void on_page_down();
    void on_page_up();

    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

    // called after config file changed, update all the UI to reflect
    // the current settings vis-a-vis status bar, etc.
    void on_config_saved();

    void set_toolbar_visibility();
    void set_statusbar_visibility();
    void set_menu_visibility();

    void show_log_file();

    void update_bookmark_panel();
    void add_bookmark();

    void setup_UI();
    void create_bookmark_panel();
    void create_menus();
    void create_file_menu(auto *);
    void create_edit_menu(auto *);
    void create_view_menu(auto *);
    void create_imslp_menu(auto *menu_bar);
    void create_global_shortcuts();

    void open_tour_dialog();
    void check_first_run_tour();

    void create_toolbar();
    void create_status_bar();

    void update_memory_usage();
    void update_background();

    void show_log_content();

    void open_file_dialog(const std::filesystem::path &pathname = "");
    void open_imslp_search_dialog();
    void open_dev_status_dialog();

    void open_fast_search_dialog();
    void open_config_dialog();
    void update_recent_files_list();
    void toggle_draw_margin() {/*TODO*/ }
    void toggle_bookmark_panel();
    void toggle_toolbar_visibility();
    void toggle_menu_visibility();
    void toggle_statusbar_visibility();
    void set_light_theme() {/*TODO*/ }
    void set_dark_theme() {/*TODO*/ }

    void update_logging_level();

    void initialize_fast_search();

    void refresh_all_documents();
    void save_config();
    void save_open_documents_to_config();
    void restore_window_state();
    void on_page_selected(int index);
    void show_page_count();

    void restore_open_documents();
    void save_window_state_to_config();
    void check_for_errors_on_exit();

    // Focuses on the specified tab
    void focus_on_tab(int index);

    bool in_single_page_mode() const;
    void toggle_page_zoom();
    void toggle_page_step();

    void toggle_text_annotation_mode();

    void on_close_tab(int index);
    void update_title(int index = 0);

    QIcon create_double_icon();
    void on_toggle_view_mode();
    void set_page_view_count(int count);

    void go_to_bookmark(int page_num);
    void update_menu_bookmark_visibility();
    void show_context_menu(const QPoint &pos);

    void reload_document();
    void edit_document(); // edit with external viewer

    // Check if the given document is open in a tab, returning either the tab index or None
    std::optional<int> doc_is_open(std::filesystem::path name);

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Mouse hide methods
    void setup_mouse_hiding();
    void reset_cursor_timer();
    bool handle_mouse_movement(QObject *watched, QEvent *event);

    void update_document_priority_order();

    void toggle_tab_visibility();


    QToolBar *main_toolbar_ = nullptr;
    StatusBar *status_bar_ = nullptr;
    QSplitter *splitter_ = nullptr;
    BookmarkPanel *bookmark_panel_ = nullptr;
    QTabWidget *tab_widget_ = nullptr;

    QMenu *open_recent_menu_ = nullptr;
    QMenu *edit_menu_ = nullptr;
    QAction *edit_margin_action_ = nullptr;
    QAction *undo_action_ = nullptr;
    QAction *redo_action_ = nullptr;
    QAction *bookmark_menu_action_ = nullptr;
    QAction *light_theme_menu_item_ = nullptr;
    QAction *dark_theme_menu_item_ = nullptr;
    QToolBar *toolbar_ = nullptr;
    QComboBox *toolbar_page_selector_ = nullptr;

    QAction *view_toggle_action_ = nullptr;
    QAction *page_step_action_ = nullptr;
    QAction *zoom_in_out_action_ = nullptr;
    QAction *margin_action_ = nullptr;
    QAction *statusbar_menu_action_ = nullptr;
    QAction *toolbar_menu_action_ = nullptr;
    QAction *menubar_menu_action_ = nullptr;
    QAction *tabs_menu_action_ = nullptr;

    QAction *text_annotation_action_ = nullptr;
    bool text_annotation_mode_ = false;

    QIcon single_icon_;
    QIcon double_icon_;
    QIcon zoomin_icon_;
    QIcon zoomout_icon_;
    QIcon page_by_1_icon_;
    QIcon page_by_2_icon_;

    QTimer *timer_ = nullptr;
    FullscreenExitButton *exit_button_ = nullptr;
    bool has_full_menu_bar_ = true;

    // we can show/hide the tabs with T key
    bool tabs_visible_ = true;

    ConfigFile config_;
    std::map<std::string, QKeySequence> shortcuts_;

    FastFileSearchDialog *fast_search_dialog_ = nullptr;

    // Mouse hiding related members
    QTimer *mouse_hide_timer_ = nullptr;
    bool cursor_hidden_ = false;

    // ensure the fast file search dialog is created
    std::mutex fast_search_mutex_;
    std::condition_variable fast_search_cv_;
    bool fast_search_initialized_ = false;

    bool restoring_documents_ = false;
    DocumentLoadManager load_manager_;

    FileViewer *m_logViewer = nullptr;

    bool was_suspended = false;

signals:
    void fastSearchInitialized();

    // Notify UI when loading is done
    void document_loaded(std::string name, int page);

};
