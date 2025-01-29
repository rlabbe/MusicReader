#pragma once

#include <QTreeWidget>


class BookmarkPanel;

class BookmarkTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit BookmarkTreeWidget(BookmarkPanel *parent);

protected:
    void dropEvent(QDropEvent *event) override;

private:
    BookmarkPanel *parent_panel_;
};


