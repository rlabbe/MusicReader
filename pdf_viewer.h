#pragma once

#include <QtWidgets>
//#include <QPixmap>
//#include <QWheelEvent>
//#include <QSwipeGesture>

//#include <QPainter>
#include "document.h"
#include "config_file.h"
#include "page.h"

class StatusBar;

class PDFViewer : public QWidget
{
    Q_OBJECT

public:
    PDFViewer(Document *document,
              ConfigFile *config,
              int page,
              StatusBar *sbar,
              QWidget *parent = nullptr);

    int page_count() const;
    int current_page() const;
    bool single_page_view() const;

    void get_page(int page_num, bool first_call = false);


    void replace_document(Document *document, int page);
    void refresh();
    void page_up();
    void page_down();
    void change_page(int step);
    void update_status_bar();
    Document *document() const { return document_; }

protected:

    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void init_ui(int page);
    Page get_single_page(int page_num);
    Page get_double_page(int page_num);
    void adjust_initial_subwindow_size();
    void update_scrollbar_visibility();
    void on_scrollbar_value_changed(int new_page);
    void _update_image(const QString &message = QString());

    QLabel *label_;
    StatusBar *status_bar_;
    QScrollBar *scrollbar_;
    QHBoxLayout *layout_;
    Document *document_;
    ConfigFile *config_;
    Page page_;
    bool drawing_margin_ = false;
    double aspect_ratio_ = 1.0;
    QRect margin_rect_;
};

