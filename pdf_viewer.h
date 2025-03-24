#pragma once

#include <QtWidgets>
#include <memory>

#include "document.h"
#include "config_file.h"
#include "page.h"

class StatusBar;

class PDFViewer : public QWidget {
    Q_OBJECT

public:

    // When document is created it isn't opened yet. So,
    // create this class, call document->load_document(), and when it is
    // complete call get_page(page_num, true). 
    PDFViewer(std::shared_ptr<Document> document,
              ConfigFile *config,
              int page,
              StatusBar *sbar,
              QWidget *parent = nullptr);

    ~PDFViewer();

    int page_count() const;
    int current_page() const;
    bool single_page_view() const;
    bool double_page_view() const { return !single_page_view(); }

    void get_page(int page_num, bool first_call = false);

    void refresh();
    void page_up();
    void page_down();
    void change_page(int step);
    void update_status_bar();

    std::shared_ptr<Document> document() const { return document_; }
    void replace_document(std::shared_ptr<Document> document, int page);

protected:

    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_page_loaded(int page_index);

private:
    void init_ui(int page);
    Page get_single_page(int page_num);
    Page get_double_page(int page_num);
    void adjust_initial_subwindow_size();
    void update_scrollbar_visibility();
    void on_scrollbar_value_changed(int new_page);
    void update_image(const QString &message = QString());

    QLabel *label_;
    StatusBar *status_bar_;
    QScrollBar *scrollbar_;
    QHBoxLayout *layout_;
    std::shared_ptr<Document> document_;
    ConfigFile *config_;
    Page page_;
    bool drawing_margin_ = false;
    double aspect_ratio_ = 1.0;
    QRect margin_rect_;

    // set to true if calling code to set the scrollbar
    // programatically so we don't request a page (the
    // caller will be doing that). Checked in 
    // on_scrollbar_value_changed().
    bool manual_scrollbar_change_ = false;
};

