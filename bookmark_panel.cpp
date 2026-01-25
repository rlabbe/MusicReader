#include "bookmark_panel.h"
#include "bookmark_titlebar.h"
#include "bookmark_treewidget.h"
#include "bookmark.h"
#include "pdf_viewer.h"
#include "exception_logger.h"


BookmarkPanel::BookmarkPanel(MusicReader* main_window)
    : QWidget(main_window)
    , main_window_(main_window)
    , visible_(true)
{
    init_ui();
}


void BookmarkPanel::dropEvent(QDropEvent* event)
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc)
        return;

    auto* target_item = tree_widget_->itemAt(event->position().toPoint());
    auto selected_items = tree_widget_->selectedItems();

    if (!target_item || selected_items.isEmpty())
        return;

    auto target_handle = handle_of(target_item);

    for (auto* item : selected_items) {
        auto handle = handle_of(item);
        doc->reparent_bookmark(handle, target_handle);
    }
    event->accept();
    populate();
}


void BookmarkPanel::init_ui()
{
    SAFE_METHOD;

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    title_bar_ = new BookmarkTitleBar(main_window_, this);
    layout->addWidget(title_bar_);

    tree_widget_ = new BookmarkTreeWidget(this, main_window_);

    connect(tree_widget_, &QTreeWidget::itemClicked, this, &BookmarkPanel::on_bookmark_clicked);
    connect(tree_widget_, &QTreeWidget::itemChanged, this, &BookmarkPanel::on_bookmark_edited);
    layout->addWidget(tree_widget_);

    button_bar_ = new QWidget();
    auto* button_layout = new QHBoxLayout(button_bar_);
    button_layout->setContentsMargins(2, 2, 2, 2);
    button_layout->setSpacing(5);

    add_button_ = new QPushButton();
    add_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    add_button_->setToolTip("Add Bookmark (Ctrl+D or B)");
    add_button_->setFocusPolicy(Qt::NoFocus);
    connect(add_button_, &QPushButton::clicked, this, &BookmarkPanel::add_bookmark);
    button_layout->addWidget(add_button_);

    delete_button_ = new QPushButton();
    delete_button_->setIcon(QIcon(":/MusicReader/images/delete.ico"));
    delete_button_->setToolTip("Delete Bookmark (Del)");
    delete_button_->setFocusPolicy(Qt::NoFocus);
    connect(delete_button_, &QPushButton::clicked, this, &BookmarkPanel::delete_selected_bookmark);
    button_layout->addWidget(delete_button_);

    // Separator
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    button_layout->addWidget(separator);

    unindent_button_ = new QPushButton();
    unindent_button_->setIcon(QIcon(":/MusicReader/images/left.ico"));
    unindent_button_->setToolTip("Unindent (Ctrl+Right)");
    unindent_button_->setFocusPolicy(Qt::NoFocus);
    unindent_button_->setEnabled(true);
    connect(unindent_button_, &QPushButton::clicked, this, &BookmarkPanel::unindent_selected_bookmarks);
    button_layout->addWidget(unindent_button_);

    // Redo button
    indent_button_ = new QPushButton();
    indent_button_->setIcon(QIcon(":/MusicReader/images/right.ico"));
    indent_button_->setToolTip("Indent (CTRL+Left)");
    indent_button_->setFocusPolicy(Qt::NoFocus);
    indent_button_->setEnabled(true);
    connect(indent_button_, &QPushButton::clicked, this, &BookmarkPanel::indent_selected_bookmarks);
    button_layout->addWidget(indent_button_);

    button_layout->addStretch();
    layout->insertWidget(1, button_bar_);

    QWidget* second_button_bar = new QWidget();
    auto* second_button_layout = new QHBoxLayout(second_button_bar);
    second_button_layout->setContentsMargins(2, 2, 2, 2);
    second_button_layout->setSpacing(5);

    follow_page_checkbox_ = new QCheckBox("Track pages");
    follow_page_checkbox_->setFocusPolicy(Qt::NoFocus);
    follow_page_checkbox_->setChecked(true); // Default to enabled
    second_button_layout->addWidget(follow_page_checkbox_);

    second_button_layout->addStretch();
    layout->insertWidget(2, second_button_bar);

    setup_context_menu();
    setup_shortcuts();
    adjust_width();
}


void BookmarkPanel::update_undo_redo_buttons()
{
    SAFE_METHOD;

    unindent_button_->setEnabled(can_undo());
    indent_button_->setEnabled(can_redo());
}


void BookmarkPanel::setup_shortcuts()
{
    SAFE_METHOD;

    // Use WidgetWithChildren context so Delete only fires when bookmark panel/tree has focus
    auto* del_shortcut = new QShortcut(QKeySequence("Del"), this, SLOT(delete_selected_bookmark()));
    del_shortcut->setContext(Qt::WidgetWithChildrenShortcut);

    new QShortcut(QKeySequence("Ctrl+Z"), this, SLOT(undo()));
    new QShortcut(QKeySequence("Ctrl+Y"), this, SLOT(redo()));
    new QShortcut(QKeySequence("Ctrl+Left"), this, SLOT(unindent_selected_bookmarks()));
    new QShortcut(QKeySequence("Ctrl+Right"), this, SLOT(indent_selected_bookmarks()));
}


void BookmarkPanel::setup_context_menu()
{
    SAFE_METHOD;

    tree_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_widget_, &QTreeWidget::customContextMenuRequested, this, &BookmarkPanel::show_context_menu);
}


void BookmarkPanel::toggle_visibility()
{
    SAFE_METHOD;

    visible_ = !visible_;
    setVisible(visible_);
    emit bookmark_visibility_changed(visible_);
}


void BookmarkPanel::show_context_menu(const QPoint& position)
{
    SAFE_METHOD;

    QMenu menu(tree_widget_);
    auto* add_action = menu.addAction("Create Bookmark");
    auto* add_nested_action = menu.addAction("Create Nested Bookmark");

    auto selected_items = tree_widget_->selectedItems();
    QAction* indent_action = nullptr;
    QAction* unindent_action = nullptr;
    QAction* delete_action = nullptr;

    if (!selected_items.isEmpty()) {
        indent_action = menu.addAction("Indent (Ctrl++)");
        unindent_action = menu.addAction("Unindent (Ctrl+-)");
        delete_action = menu.addAction("Delete (DEL)");
    }

    QAction* action = menu.exec(tree_widget_->viewport()->mapToGlobal(position));

    if (action == add_action)
        add_bookmark();
    else if (action == add_nested_action && !selected_items.isEmpty())
        ; // TODO add_child_bookmark(selected_items.last());
    else if (action == indent_action)
        indent_selected_bookmarks();
    else if (action == unindent_action)
        unindent_selected_bookmarks();
    else if (action == delete_action)
        delete_selected_bookmark();
}


void BookmarkPanel::add_items(const std::vector<Bookmark>& bookmarks, QTreeWidgetItem* parent)
{
    SAFE_METHOD;

    for (const auto& bookmark : bookmarks) {
        QString title = QString::fromStdString(bookmark.title_);
        title.replace('\n', ' ').replace('\r', ' '); // don't allow newlines in titles

        auto* item = new QTreeWidgetItem(QStringList() << title);
        set_item_info(item, bookmark);

        if (parent)
            parent->addChild(item);
        else
            tree_widget_->addTopLevelItem(item);

        if (!bookmark.children_.empty())
            add_items(bookmark.children_, item);
    }
}


void BookmarkPanel::populate()
{
    SAFE_METHOD;

    tree_widget_->clear();
    auto doc = document();
    if (!doc || doc->bookmarks().empty())
        return;

    add_items(doc->bookmarks(), nullptr); // Start with top-level bookmarks

    tree_widget_->expandAll();
    adjust_width();
}


void BookmarkPanel::adjust_width()
{
    SAFE_METHOD;

    tree_widget_->resizeColumnToContents(0);
    int content_width = tree_widget_->header()->sectionSizeHint(0);
    setMinimumWidth(std::max(content_width + 20, 100));
}


void BookmarkPanel::on_bookmark_clicked(QTreeWidgetItem* item, int)
{
    SAFE_METHOD;

    int page_num = page_num_of(item);
    emit bookmark_clicked(page_num);
}


std::shared_ptr<Document> BookmarkPanel::document() const
{
    SAFE_METHOD;

    return main_window_->current_document();
}


void BookmarkPanel::on_bookmark_edited(QTreeWidgetItem* item, int)
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc)
        return;

    auto new_title = title_of(item);
    auto handle = handle_of(item);

    if (new_title.empty())
        return;

    doc->rename_bookmark(handle, new_title);
}


void BookmarkPanel::delete_selected_bookmark()
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc)
        return;

    auto selected_items = tree_widget_->selectedItems();

    if (selected_items.isEmpty())
        return;

    for (auto* item : selected_items) {
        auto handle = handle_of(item);
        doc->remove_bookmark(handle);
    }
    populate();
}


void BookmarkPanel::add_bookmark()
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc)
        return;

    auto [page_num, valid] = main_window_->current_page("no current page in add_bookmark");
    auto [handle, save_succcess] = doc->add_bookmark("Untitled", page_num);
    setVisible(true);
    populate();

    if (!save_succcess) {
        logger::error("Failed to save bookmark");
        main_window_->display_error_message("Failed to save bookmark");
        return;
    }

    // enter edit mode, because you almost certainly want to change
    // the name from "Untitled"
    auto* item = find_item_by_handle(handle);
    if (item) {
        tree_widget_->setCurrentItem(item);
        tree_widget_->editItem(item, 0);
    }
}


QTreeWidgetItem* BookmarkPanel::find_item_recursive(QTreeWidgetItem* item, const BookmarkHandle& handle)
{
    SAFE_METHOD;

    if (handle_of(item) == handle)
        return item;

    for (int i = 0; i < item->childCount(); ++i) {
        if (auto* found = find_item_recursive(item->child(i), handle))
            return found;
    }
    return nullptr;
}


QTreeWidgetItem* BookmarkPanel::find_item_by_handle(const BookmarkHandle& handle)
{
    SAFE_METHOD;

    for (int i = 0; i < tree_widget_->topLevelItemCount(); ++i) {
        if (auto* found = find_item_recursive(tree_widget_->topLevelItem(i), handle))
            return found;
    }
    return nullptr;
}


bool BookmarkPanel::can_undo() const
{
    SAFE_METHOD;

    auto doc = document();
    return doc && doc->can_undo();
}


bool BookmarkPanel::can_redo() const
{
    SAFE_METHOD;

    auto doc = document();
    return doc && doc->can_redo();
}


void BookmarkPanel::undo()
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc)
        return;

    doc->undo();
    populate();
}


void BookmarkPanel::redo()
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc)
        return;

    doc->redo();
    populate();
}


BookmarkHandle BookmarkPanel::handle_of(QTreeWidgetItem* item) const
{
    SAFE_METHOD;

    if (item)
        return BookmarkHandle(item->data(0, Qt::UserRole + 1).toInt());

    logger::error("nullptr to item");
    return {};
}


int BookmarkPanel::page_num_of(QTreeWidgetItem* item) const
{
    SAFE_METHOD;

    return item->data(0, Qt::UserRole).toInt();
}


std::string BookmarkPanel::title_of(QTreeWidgetItem* item) const
{
    SAFE_METHOD;

    return item->text(0).trimmed().toStdString();
}


void BookmarkPanel::set_item_info(QTreeWidgetItem* item, const Bookmark& bookmark)
{
    SAFE_METHOD;

    item->setData(0, Qt::UserRole, bookmark.page_num_ ? QVariant(*bookmark.page_num_) : QVariant());
    item->setData(0, Qt::UserRole + 1, int(bookmark.handle_));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}


QList<int> BookmarkPanel::selected_rows() const
{
    SAFE_METHOD;

    auto selected_items = tree_widget_->selectedItems();
    auto* parent = selected_items.first()->parent();

    QList<int> rows;
    for (auto* item : selected_items)
        rows.append(parent ? parent->indexOfChild(item) : tree_widget_->indexOfTopLevelItem(item));

    std::sort(rows.begin(), rows.end());
    return rows;
}


bool BookmarkPanel::is_bookmark_selected(bool indent) const
{
    SAFE_METHOD;

    auto selected_items = tree_widget_->selectedItems();
    if (selected_items.size() < 1)
        return false;

    // All items must have the same parent
    auto* parent = selected_items.first()->parent();
    for (auto* item : selected_items) {
        if (item->parent() != parent)
            return false;
    }

    // All items must be visually contiguous
    QList<int> rows = selected_rows();
    for (int i = 1; i < rows.size(); ++i) {
        if (rows[i] != rows[i - 1] + 1)
            return false; // Not contiguous
    }

    if (indent) {
        // Ensure the first selected item has a previous sibling
        int first_row = rows.first();
        if (first_row == 0)
            return false; // No previous sibling to indent under
    }
    return true;
}


void BookmarkPanel::indent_selected_bookmarks()
{
    SAFE_METHOD;

    auto doc = document();

    if (!doc || !is_bookmark_selected(true))
        return;

    auto selected_items = tree_widget_->selectedItems();

    // Store handles of selected bookmarks
    std::vector<BookmarkHandle> selected_handles;
    for (auto* item : selected_items)
        selected_handles.push_back(handle_of(item));

    auto* parent = selected_items.first()->parent();
    QList<int> rows = selected_rows();
    int first_row = rows.first();

    QTreeWidgetItem* new_parent = parent ? parent->child(first_row - 1) : tree_widget_->topLevelItem(first_row - 1);

    if (!new_parent)
        return;

    for (auto* item : selected_items) {
        BookmarkHandle handle = handle_of(item);
        BookmarkHandle new_parent_handle = handle_of(new_parent);
        doc->reparent_bookmark(handle, new_parent_handle);
    }

    populate();

    // Restore selection
    tree_widget_->clearSelection();
    for (const auto& handle : selected_handles) {
        auto* item = find_item_by_handle(handle);
        if (item) {
            item->setSelected(true);
        }
    }
}


void BookmarkPanel::unindent_selected_bookmarks()
{
    SAFE_METHOD;

    auto doc = document();
    if (!doc || !is_bookmark_selected(false))
        return;

    auto selected_items = tree_widget_->selectedItems();

    // Store handles of selected bookmarks
    std::vector<BookmarkHandle> selected_handles;
    for (auto* item : selected_items)
        selected_handles.push_back(handle_of(item));

    for (auto* item : selected_items) {
        auto handle = handle_of(item);
        doc->unindent_bookmark(handle);
    }

    populate();

    // Restore selection
    tree_widget_->clearSelection();
    for (const auto& handle : selected_handles) {
        auto* item = find_item_by_handle(handle);
        if (item)
            item->setSelected(true);
    }
}


void BookmarkPanel::select_page(int page_num)
{
    SAFE_METHOD;

    if (tracking_paused_)
        return;
    if (!follow_page_checkbox_->isChecked())
        return;
    auto doc = document();
    if (!doc)
        return;

    auto [bookmark_page, target_handle] = find_bookmark_for_page(page_num, doc->bookmarks());

    // Only select if the bookmark is exactly on this page, otherwise clear selection
    if (bookmark_page != page_num) {
        tree_widget_->clearSelection();
        tree_widget_->setCurrentItem(nullptr);
        return;
    }

    // Find the tree widget item for this bookmark
    auto* item = find_item_by_handle(target_handle);
    if (!item)
        return;

    // Clear current selection and select the found item
    tree_widget_->clearSelection();
    tree_widget_->setCurrentItem(item);
    tree_widget_->scrollToItem(item);
    tree_widget_->setFocus();
}


std::vector<std::pair<int, BookmarkHandle>> BookmarkPanel::flatten_bookmarks(const std::vector<Bookmark>& bookmarks)
{
    std::vector<std::pair<int, BookmarkHandle>> flattened;

    std::function<void(const std::vector<Bookmark>&)> flatten = [&](const std::vector<Bookmark>& bmarks) {
        for (const auto& bookmark : bmarks) {
            if (bookmark.page_num_.has_value())
                flattened.emplace_back(bookmark.page_num_.value(), bookmark.handle_);

            if (!bookmark.children_.empty())
                flatten(bookmark.children_);
        }
    };

    flatten(bookmarks);
    return flattened;
}


std::pair<int, BookmarkHandle> BookmarkPanel::find_bookmark_for_page(int page_num, const std::vector<Bookmark>& bookmarks)
{
    auto flattened = flatten_bookmarks(bookmarks);

    // Find the last bookmark with page <= page_num
    auto it = std::upper_bound(flattened.begin(), flattened.end(), page_num, [](int page, const auto& bookmark) {
        return page < bookmark.first;
    });

    if (it != flattened.begin()) {
        --it;
        return *it;
    }

    return {-1, BookmarkHandle()};
}


BookmarkHandle BookmarkPanel::find_bookmark_by_page(int page_num, const std::vector<Bookmark>& bookmarks)
{
    auto flattened = flatten_bookmarks(bookmarks);

    // Binary search for the first bookmark with page == page_num
    auto it = std::lower_bound(flattened.begin(), flattened.end(), page_num, [](const auto& bookmark, int page) {
        return bookmark.first < page;
    });

    if (it != flattened.end() && it->first == page_num)
        return it->second; // Exact match - returns first bookmark on this page

    return BookmarkHandle();
}
