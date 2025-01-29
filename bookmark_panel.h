#pragma once

#include <QtWidgets>

class BookmarkTitleBar;
class BookmarkTreeWidget;
class MusicReader;
class Bookmark;
class Document;


class BookmarkPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BookmarkPanel(MusicReader *main_window);
    void dropEvent(QDropEvent *event) override;

    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    // Adjusts the panel width to fit content
    void adjust_width();

signals:
    void bookmark_visibility_changed(bool visible);
    void bookmark_clicked(int page_num);


private:

    // extract info stored with tree item
    std::string handle_of(QTreeWidgetItem *item) const;
    int page_num_of(QTreeWidgetItem *item) const;
    std::string title_of(QTreeWidgetItem *item) const;

    // and set them
    void set_item_info(QTreeWidgetItem *item, int page_num,const std::string &handle);

    // Moves selected bookmarks one level deeper (into the previous bookmark)
    void indent_selected_bookmarks();
    
    // Moves selected bookmarks up one level (out of their parent)
    void unindent_selected_bookmarks();

    void init_ui();

    void setup_shortcuts();

    void setup_context_menu();

    void toggle_visibility();
    void show_context_menu(const QPoint &position);

    // Populates the tree widget with the document's bookmarks
    void populate();

    void on_bookmark_clicked(QTreeWidgetItem *item, int);
    void on_bookmark_edited(QTreeWidgetItem *item, int);
    void delete_selected_bookmark();

    // Adds a new bookmark at the current page.
    void add_bookmark();

    // Finds a tree widget item by its bookmark handle.
    QTreeWidgetItem *find_item_by_handle(const std::string &handle);

    Document *document() const;

    MusicReader *main_window_;
    bool visible_;
    BookmarkTitleBar *title_bar_;
    BookmarkTreeWidget *tree_widget_;
    QWidget *button_bar_;
    QPushButton *add_button_;
    QPushButton *delete_button_;
};
