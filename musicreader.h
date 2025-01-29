#pragma once

#include <QtWidgets>
#include <memory>
#include <optional>
#include <filesystem>
#include "config_file.h"

class BookmarkPanel;
class Document;
class PDFViewer;
class StatusBar;

class MusicReader : public QMainWindow
{
    Q_OBJECT

public:
    MusicReader(QWidget *parent = nullptr);

    Document *current_document(const std::string &log_msg = "") const;
    std::pair<int, bool> current_page(const std::string &log_msg="") const;

signals:
    void view_mode_signal_(int page_view_count);

private:
    void setup_UI();
    void create_bookmark_panel();
    void create_menus();
    void create_toolbar();
    void create_status_bar();

    void update_memory_usage();

    void open_file_dialog(const std::string &pathname="");
    void open_fast_search_dialog() {/*TODO*/ }
    void open_config_dialog();
    void update_recent_files_list() {/*TODO*/ }
    void update_dpi_setting(bool recompute) {/*TODO*/ }
    void toggle_draw_margin() {/*TODO*/ }
    void toggle_bookmark_panel() {/*TODO*/ }
    void toggle_toolbar_visibility() {/*TODO*/ }
    void toggle_statusbar_visibility() {/*TODO*/ }
    void set_light_theme() {/*TODO*/ }
    void set_dark_theme() {/*TODO*/ }
    void update_undo_redo_state() {/*TODO*/ }

    void refresh_all_documents();
    void save_open_documents_to_config();
    void restore_window_state();
    void on_page_selected(int index);
    void show_page_count();

    PDFViewer *current_tab() const;
    std::string current_document_name() const;
    Document *document_at(int index) const;
    PDFViewer *viewer_tab(int index) const;
    PDFViewer *current_viewer(const std::string &log_err = "") const;
    void open_pdf_in_tab(const std::string &filename, int page=1);
    Document *open_pdf_document(const std::string &filename);

    void display_error_message(const std::string &msg);
    bool display_query(const std::string &msg);


    // Focuses on the specified tab
    void focus_on_tab(int index);

    bool in_single_page_mode() const;
    void toggle_page_zoom();
    void on_page_down();
    void on_page_up();


    QIcon create_double_icon();
    void on_toggle_view_mode();


    void go_to_bookmark(int page_num);
    void update_menu_bookmark_visibility();

    // Check if the given document is open in a tab, returning either the tab index or None
    std::optional<int> doc_is_open(std::filesystem::path name);

    QMenuBar *menu_bar_;
    QToolBar *main_toolbar_;
    StatusBar *status_bar_;
    QSplitter *splitter_;
    BookmarkPanel *bookmark_panel_;
    QTabWidget *tab_widget_;


private:
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
    QIcon single_icon_;
    QIcon double_icon_;
    QIcon zoomin_icon_;
    QIcon zoomout_icon_;
    QTimer* timer_;

    ConfigFile config_;
    std::map<std::string, QKeySequence> shortcuts_;

};
