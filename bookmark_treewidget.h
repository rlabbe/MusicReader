#pragma once

#include <QTreeWidget>


class BookmarkPanel;
class MusicReader;

class BookmarkTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit BookmarkTreeWidget(BookmarkPanel* parent, MusicReader* main_window);

protected:
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawBranches(QPainter*, const QRect&, const QModelIndex&) const override {};


private:
    BookmarkPanel* parent_panel_;
    MusicReader* main_window_;
};
