#include "bookmark_panel.h"
#include "bookmark_titlebar.h"
#include "bookmark_treewidget.h"
#include "document.h"
#include "logger.h"
#include "bookmark.h"
#include "pdf_viewer.h"

BookmarkPanel::BookmarkPanel(MusicReader *main_window)
    : QWidget(main_window)
    , main_window_(main_window)
    , visible_(true)
{
    init_ui();
}

void BookmarkPanel::dropEvent(QDropEvent *event)
{
    Document *doc = document();
    if (!doc) return;

    auto *target_item = tree_widget_->itemAt(event->position().toPoint());
    auto selected_items = tree_widget_->selectedItems();

    if (!target_item || selected_items.isEmpty()) return;
    auto target_handle = handle_of(target_item);

    for (auto *item : selected_items)
    {
        auto handle = handle_of(item);
        doc->reparent_bookmark(handle, target_handle);
    }
    event->accept();
    populate();
}


void BookmarkPanel::init_ui()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    title_bar_ = new BookmarkTitleBar(main_window_, this);
    layout->addWidget(title_bar_);

    tree_widget_ = new BookmarkTreeWidget(this);
    tree_widget_->setHeaderHidden(true);
    connect(tree_widget_, &QTreeWidget::itemClicked, this, &BookmarkPanel::on_bookmark_clicked);
    connect(tree_widget_, &QTreeWidget::itemChanged, this, &BookmarkPanel::on_bookmark_edited);
    tree_widget_->setEditTriggers(QTreeWidget::DoubleClicked);
    tree_widget_->setDragEnabled(true);
    tree_widget_->setDefaultDropAction(Qt::MoveAction);
    tree_widget_->setDropIndicatorShown(true);
    tree_widget_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_widget_->viewport()->setAcceptDrops(true);
    layout->addWidget(tree_widget_);

    button_bar_ = new QWidget();
    auto *button_layout = new QHBoxLayout(button_bar_);
    button_layout->setContentsMargins(2, 2, 2, 2);
    button_layout->setSpacing(5);

    add_button_ = new QPushButton();
    add_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    add_button_->setToolTip("Add Bookmark (Ctrl+D)");
    connect(add_button_, &QPushButton::clicked, this, &BookmarkPanel::add_bookmark);
    button_layout->addWidget(add_button_);

    delete_button_ = new QPushButton();
    delete_button_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    delete_button_->setToolTip("Delete Bookmark (Del)");
    connect(delete_button_, &QPushButton::clicked, this, &BookmarkPanel::delete_selected_bookmark);
    button_layout->addWidget(delete_button_);

    button_layout->addStretch();
    layout->insertWidget(1, button_bar_);

    setup_context_menu();
    setup_shortcuts();
    adjust_width();
}

void BookmarkPanel::setup_shortcuts()
{
    new QShortcut(QKeySequence("Ctrl+B"), this, SLOT(toggle_visibility()));
    new QShortcut(QKeySequence("Ctrl+D"), this, SLOT(add_bookmark()));
    new QShortcut(QKeySequence("Del"), this, SLOT(delete_selected_bookmark()));
    new QShortcut(QKeySequence("Ctrl+Z"), this, SLOT(undo()));
    new QShortcut(QKeySequence("Ctrl+Y"), this, SLOT(redo()));
}

void BookmarkPanel::setup_context_menu()
{
    tree_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_widget_, &QTreeWidget::customContextMenuRequested, this, &BookmarkPanel::show_context_menu);
}

void BookmarkPanel::toggle_visibility()
{
    visible_ = !visible_;
    setVisible(visible_);
    emit bookmark_visibility_changed(visible_);
}

void BookmarkPanel::show_context_menu(const QPoint &position)
{
    QMenu menu(tree_widget_);
    auto *add_action = menu.addAction("Create Bookmark");
    auto *add_nested_action = menu.addAction("Create Nested Bookmark");

    auto selected_items = tree_widget_->selectedItems();
    QAction *indent_action = nullptr;
    QAction *unindent_action = nullptr;

    if (!selected_items.isEmpty())
    {
        indent_action = menu.addAction("Indent");
        unindent_action = menu.addAction("Unindent");
    }

    QAction *action = menu.exec(tree_widget_->viewport()->mapToGlobal(position));

    if (action == add_action)
    {
        add_bookmark();
    }
    else if (action == add_nested_action && !selected_items.isEmpty())
    {
        ; //TODO add_child_bookmark(selected_items.last());
    }
    else if (action == indent_action)
    {
        indent_selected_bookmarks();
    }
    else if (action == unindent_action)
    {
        unindent_selected_bookmarks();
    }
}

void BookmarkPanel::populate()
{
    tree_widget_->clear();

    Document *doc = document();
    if (!doc || doc->bookmarks().empty()) return;

    std::function<void(const std::vector<Bookmark> &, QTreeWidgetItem *)> add_items =
        [&](const std::vector<Bookmark> &bookmarks, QTreeWidgetItem *parent) {
        for (const auto &bookmark : bookmarks) {
            auto *item = new QTreeWidgetItem(QStringList() << QString::fromStdString(bookmark.title_));
            item->setData(0, Qt::UserRole, bookmark.page_num_ ? QVariant(*bookmark.page_num_) : QVariant());
            item->setData(0, Qt::UserRole + 1, QString::fromStdString(bookmark.handle_));
            item->setFlags(item->flags() | Qt::ItemIsEditable);

            if (parent)
                parent->addChild(item);  // **Fix: Ensure items are added to the correct parent**
            else
                tree_widget_->addTopLevelItem(item);  // **Fix: Add top-level items correctly**

            if (!bookmark.children_.empty()) {
                add_items(bookmark.children_, item);
            }
        }
    };

    add_items(doc->bookmarks(), nullptr);  // Start with top-level bookmarks

    tree_widget_->expandAll();
    adjust_width();
    return_focus_to_main();
}


/*
void BookmarkPanel::populate()
{
    tree_widget_->clear();

    Document *doc = document();

    if (!doc || doc->bookmarks().empty())
    {
        // TODO? tree_widget_->setMinimumWidth(80);
        return;
    }

    std::function<void(const std::vector<Bookmark> &, QTreeWidgetItem *)> add_items =
        [&](const std::vector<Bookmark> &bookmarks, QTreeWidgetItem *parent)
    {
        for (const auto &bookmark : bookmarks)
        {
            auto *item = new QTreeWidgetItem(parent, QStringList() << QString::fromStdString(bookmark.title_));
            item->setData(0, Qt::UserRole, bookmark.page_num_ ? QVariant(*bookmark.page_num_) : QVariant());
            item->setData(0, Qt::UserRole + 1, QString::fromStdString(bookmark.handle_));
            item->setFlags(item->flags() | Qt::ItemIsEditable);

            if (!bookmark.children_.empty())
            {
                add_items(bookmark.children_, item);
            }
        }
    };

    for (const auto &bookmark : doc->bookmarks())
    {
        add_items({ bookmark }, nullptr);
    }

    tree_widget_->expandAll();
    adjust_width();
}
*/
void BookmarkPanel::return_focus_to_main()
{
    if (!main_window_) 
        return;

    auto *current_viewer = main_window_->current_viewer();
    if (current_viewer) 
        current_viewer->setFocus();
    
}


void BookmarkPanel::adjust_width()
{
    tree_widget_->resizeColumnToContents(0);
    int content_width = tree_widget_->header()->sectionSizeHint(0);
    setMinimumWidth(std::max(content_width + 20, 100));
}

void BookmarkPanel::on_bookmark_clicked(QTreeWidgetItem *item, int)
{
    int page_num = page_num_of(item);
    emit bookmark_clicked(page_num);
    return_focus_to_main();
}


Document *BookmarkPanel::document() const
{
    auto doc = main_window_->current_document();
    if (!doc)
    {
        logger::log_error("No document found");
    }
    return doc;
}


void BookmarkPanel::on_bookmark_edited(QTreeWidgetItem *item, int)
{
    Document *doc = document();
    if (!doc)
        return;

    auto new_title = title_of(item);
    auto handle = handle_of(item);

    if (new_title.empty()) return;

    doc->rename_bookmark(handle, new_title);
    return_focus_to_main();
}


void BookmarkPanel::delete_selected_bookmark()
{
    auto *item = tree_widget_->currentItem();
    Document *doc = document();
    if (!doc)
        return;

    auto handle = handle_of(item);
    doc->remove_bookmark(handle);
    populate();
    return_focus_to_main();
}


void BookmarkPanel::add_bookmark()
{
    auto *doc = document();
    if (!doc)
        return;

    auto [page_num, valid] = main_window_->current_page("no current page in add_bookmark");

    auto new_bookmark = doc->add_bookmark("Untitled", page_num);
    setVisible(true);
    populate();
    if (auto *item = find_item_by_handle(new_bookmark.handle_); item)
    {
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        tree_widget_->setCurrentItem(item);
        tree_widget_->editItem(item, 0);
    }
    return_focus_to_main();
}

QTreeWidgetItem *BookmarkPanel::find_item_by_handle(const std::string &handle)
{
    auto items = tree_widget_->findItems("", Qt::MatchExactly | Qt::MatchRecursive, 0);
    for (auto *item : items)
    {
        if (handle_of(item) == handle)
        {
            return item;
        }
    }
    return nullptr;
}

bool BookmarkPanel::can_undo() const
{
    const auto *doc = document();
    return doc && doc->can_undo();
}

bool BookmarkPanel::can_redo() const
{
    const auto *doc = document();
    return doc && doc->can_redo();
}

void BookmarkPanel::undo()
{
    auto *doc = document();
    if (!doc)
    {
        return;
    }

    doc->undo();
    populate();
}

void BookmarkPanel::redo()
{
    auto *doc = document();
    if (!doc)
    {
        return;
    }

    doc->redo();
    populate();
}


/*

void BookmarkPanel::add_child_bookmark()
{
    //TODO
    // code by chatgpt, almost certainly wrong

    auto *doc = document();
    if (!doc)
        return;

    auto selected_items = tree_widget_->selectedItems();
    if (selected_items.isEmpty())
        return;

    auto *parent_item = selected_items.last();
    std::string parent_handle = handle_of(parent_item);
    auto *parent_bookmark = doc->find_bookmark(parent_handle);
    if (!parent_bookmark)
        return;

    Bookmark new_bookmark("Untitled");
    parent_bookmark->add_child(new_bookmark);

    populate(); // Refresh the tree

    auto *new_item = find_item_by_handle(new_bookmark.handle_);
    if (!new_item)
        return;

    new_item->setFlags(new_item->flags() | Qt::ItemIsEditable);
    tree_widget_->setCurrentItem(new_item);
    tree_widget_->editItem(new_item, 0);
}
*/


std::string BookmarkPanel::handle_of(QTreeWidgetItem *item) const
{
    if (!item)
    {
        logger::log_error("nullptr to item");
        return "";
    }

    return item ? item->data(0, Qt::UserRole + 1).toString().toStdString() : "";
}

int BookmarkPanel::page_num_of(QTreeWidgetItem *item) const
{
    if (!item)
    {
        logger::log_error("nullptr to item");
        return 1;
    }

    return item->data(0, Qt::UserRole).toInt();
}

std::string BookmarkPanel::title_of(QTreeWidgetItem *item) const
{
    if (!item)
    {
        logger::log_error("nullptr to item");
        return "";
    }
    return item->text(0).trimmed().toStdString();
}


void BookmarkPanel::set_item_info(QTreeWidgetItem *item, int page_num, const std::string &handle)
{
    item->setData(0, Qt::UserRole, page_num);
    item->setData(0, Qt::UserRole + 1, QString::fromStdString(handle));
}


void BookmarkPanel::indent_selected_bookmarks()
{
    Document *doc = document();
    if (!doc)
        return;

    auto selected_items = tree_widget_->selectedItems();
    if (selected_items.size() != 1) return;
    auto *item = selected_items.first();

    auto handle = handle_of(item);
    doc->indent_bookmark(handle);
    populate();
}


void BookmarkPanel::unindent_selected_bookmarks()
{
    Document *doc = document();
    if (!doc)
        return;

    auto selected_items = tree_widget_->selectedItems();
    if (selected_items.size() != 1) return;
    auto *item = selected_items.first();
    auto handle = handle_of(item);

    doc->unindent_bookmark(handle);
    populate();


}


