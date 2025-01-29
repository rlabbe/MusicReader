#include "bookmark_treewidget.h"

#include "bookmark_panel.h"



BookmarkTreeWidget::BookmarkTreeWidget(BookmarkPanel *parent)
    : QTreeWidget(parent)
    , parent_panel_(parent)
{

    setHeaderHidden(true);
    setEditTriggers(QTreeWidget::DoubleClicked);
    setDragEnabled(true);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    viewport()->setAcceptDrops(true);
}


void BookmarkTreeWidget::dropEvent(QDropEvent *event)
{

    parent_panel_->dropEvent(event);
}

