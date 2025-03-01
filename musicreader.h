#pragma once

#include <QtWidgets>
#include <memory>
#include <optional>
#include <filesystem>
#include <mutex>
#include <condition_variable>

#include "config_file.h"

class BookmarkPanel;
class Document;
class PDFViewer;
class StatusBar;
class FastFileSearchDialog;

class MusicReader : public QMainWindow
{
    Q_OBJECT

public:
    MusicReader(QWidget *parent = nullptr);

    Document *current_document(const std::string &log_msg = "") const;
    std::pair<int, bool> current_page(const std::string &log_msg = "") const;


    void on_page_down();
    void on_page_up();
    void on_page_left();
    void on_page_right();


    void display_error_message(const std::string &msg);
    bool display_query(const std::string &msg);



signals:
    void view_mode_signal_(int page_view_count);

private slots:
    void show_titlebar_menu();

private:

    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

    void update_bookmark_panel(int index);
    void add_bookmark();

    void setup_UI();
    void create_bookmark_panel();
    void create_menus();
    void create_toolbar();
    void create_status_bar();

    void update_memory_usage();
    void update_background();

    bool logged_error() { return false; } //TODO
    void show_log_content();

    void open_file_dialog(const std::string &pathname = "");
    void open_fast_search_dialog();
    void open_config_dialog();
    void update_recent_files_list() {/*TODO*/ }
    void update_dpi_setting(bool recompute) {/*TODO*/ }
    void toggle_draw_margin() {/*TODO*/ }
    void toggle_bookmark_panel();
    void toggle_toolbar_visibility() {/*TODO*/ }
    void toggle_statusbar_visibility();
    void set_light_theme() {/*TODO*/ }
    void set_dark_theme() {/*TODO*/ }
    void update_undo_redo_state() {/*TODO*/ }

    void initialize_fast_search();

    void refresh_all_documents();
    void save_config();
    void save_open_documents_to_config();
    void restore_window_state();
    void on_page_selected(int index);
    void show_page_count();

    void restore_open_documents();
    void reopen_all_documents();
    void save_window_state_to_config();
    void check_for_errors_on_exit();

public:
    PDFViewer *current_tab() const;
    std::string current_document_name() const;
    Document *document_at(int index) const;
    PDFViewer *viewer_tab(int index) const;
    PDFViewer *current_viewer(const std::string &log_err = "") const;
    PDFViewer *open_pdf_in_tab(const std::string &filename, int page = 1);
    Document *open_pdf_document(const std::string &filename);

private:
    // Focuses on the specified tab
    void focus_on_tab(int index);

    bool in_single_page_mode() const;
    void toggle_page_zoom();

    void on_close_tab(int index);
    void update_title(int index = 0);

    QIcon create_double_icon();
    void on_toggle_view_mode();

    void go_to_bookmark(int page_num);
    void update_menu_bookmark_visibility();

    // Check if the given document is open in a tab, returning either the tab index or None
    std::optional<int> doc_is_open(std::filesystem::path name);

    QToolBar *main_toolbar_;
    StatusBar *status_bar_;
    QSplitter *splitter_;
    BookmarkPanel *bookmark_panel_;
    QTabWidget *tab_widget_;

private:

    void keyPressEvent(QKeyEvent *event) override;

    QMenu *open_recent_menu_;
    QMenu *edit_menu_;
    QAction *edit_margin_action_;
    QAction *undo_action_;
    QAction *redo_action_;
    QAction *bookmark_menu_action_;
    QAction *light_theme_menu_item_;
    QAction *dark_theme_menu_item_;
    QToolBar *toolbar_;
    QAction *view_toggle_action_;
    QAction *zoom_in_out_action_;
    QAction *margin_action_;
    QAction *statusbar_menu_action_;
    QIcon single_icon_;
    QIcon double_icon_;
    QIcon zoomin_icon_;
    QIcon zoomout_icon_;
    QTimer *timer_;
    bool has_full_menu_bar_ = true; 

    ConfigFile config_;
    std::map<std::string, QKeySequence> shortcuts_;

    FastFileSearchDialog *fast_search_dialog_ = nullptr;
    // used to wait until the fast search dialog is ready
    std::mutex fast_search_mutex_;
    std::condition_variable fast_search_cv_;
signals:
    void fastSearchInitialized();
};
