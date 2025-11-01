#include "bookmark_treewidget.h"
#include <iostream>
#include "bookmark_panel.h"
#include "musicreader.h"


BookmarkTreeWidget::BookmarkTreeWidget(BookmarkPanel* parent, MusicReader* main_window)
    : QTreeWidget(parent)
    , parent_panel_(parent)
    , main_window_(main_window)
{
    assert(main_window_);
    setEditTriggers(QTreeWidget::DoubleClicked);
    setDragEnabled(true);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setItemsExpandable(false);
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setStyleSheet("QTreeView::branch { image: none; }");
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setWordWrap(false);
    viewport()->setAcceptDrops(true);
}


void BookmarkTreeWidget::dropEvent(QDropEvent* event)
{
    parent_panel_->dropEvent(event);
}


void BookmarkTreeWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Left:
        case Qt::Key_Right: QCoreApplication::sendEvent(main_window_, event); break;
        default:
            if (event->text().isEmpty() || !event->text().at(0).isPrint())
                QTreeWidget::keyPressEvent(event);
            else
                event->ignore();
    }
}
