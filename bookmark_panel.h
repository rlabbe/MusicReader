#pragma once

#include <QtWidgets>
#include "bookmark.h"

class BookmarkTitleBar;
class BookmarkTreeWidget;
class MusicReader;
class Document;

class BookmarkPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BookmarkPanel(MusicReader *main_window);
    void dropEvent(QDropEvent *event) override;

    bool can_undo() const;
    bool can_redo() const;

    // Populates the tree widget with the document's bookmarks
    void populate();

    // Adjusts the panel width to fit content
    void adjust_width();

    // select bookmark on this page (if it exists)
    void select_page(int page_num);

signals:
    void bookmark_visibility_changed(bool visible);
    void bookmark_clicked(int page_num);

public slots:
    void undo();
    void redo();

    void add_bookmark(); // Adds a new bookmark at the current page.
    void delete_selected_bookmark();
    void toggle_visibility();

    // Moves selected bookmarks one level deeper (into the previous bookmark)
    void indent_selected_bookmarks();

    // Moves selected bookmarks up one level (out of their parent)
    void unindent_selected_bookmarks();

    // there are operations that cause a lot of signals about page changes to be emitted
    // rapidly, and we don't want to track those as they are momentary/internal.
    void pause_tracking() { tracking_paused_ = true; }
    void resume_tracking() { tracking_paused_ = false; }


private:

    BookmarkHandle find_bookmark_by_page(int page_num, const std::vector<Bookmark> &bookmarks);

    BookmarkHandle find_bookmark_for_page(int page_num, const std::vector<Bookmark> &bookmarks);
    std::vector<std::pair<int, BookmarkHandle>> flatten_bookmarks(const std::vector<Bookmark> &bookmarks);
    
    void return_focus_to_main();

    void add_items(const std::vector<Bookmark> &bookmarks, QTreeWidgetItem *parent);
    QTreeWidgetItem *find_item_recursive(QTreeWidgetItem *item, const BookmarkHandle &handle);

    // extract info stored with tree item
    BookmarkHandle handle_of(QTreeWidgetItem *item) const;
    int page_num_of(QTreeWidgetItem *item) const;
    std::string title_of(QTreeWidgetItem *item) const;

    // and set them
    void set_item_info(QTreeWidgetItem *item, const Bookmark &bookmark);

    void init_ui();
    void setup_shortcuts();
    void setup_context_menu();

    void show_context_menu(const QPoint &position);

    void on_bookmark_clicked(QTreeWidgetItem *item, int);
    void on_bookmark_edited(QTreeWidgetItem *item, int);
    void update_undo_redo_buttons();
    QList<int> selected_rows() const;


    // true iff the selection allows for indent/unindent
    bool is_bookmark_selected(bool indent) const;

    // Finds a tree widget item by its bookmark handle.
    QTreeWidgetItem *find_item_by_handle(const BookmarkHandle &handle);

    std::shared_ptr<Document> document() const;

    MusicReader *main_window_ = nullptr;
    bool visible_ = false;
    BookmarkTitleBar *title_bar_ = nullptr;
    BookmarkTreeWidget *tree_widget_ = nullptr;
    QWidget *button_bar_ = nullptr;
    QPushButton *add_button_ = nullptr;
    QPushButton *delete_button_ = nullptr;
    QPushButton *indent_button_ = nullptr;
    QPushButton *unindent_button_ = nullptr;
    QCheckBox *follow_page_checkbox_ = nullptr;

    bool tracking_paused_ = false;
};
