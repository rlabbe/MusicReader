#pragma once

#include <QtWidgets>
#include <memory>

#include "document.h"
#include "page.h"
#include "font_info.h"
#include "page_renderer.h"
#include "performance_mode.h"

class StatusBar;
class ConfigFile;
class InPlaceAnnotationEditor;
class MusicReader;
class BookmarkPanel;


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
              QWidget *parent,
              MusicReader *reader,
              BookmarkPanel *panel);

    ~PDFViewer();

    int page_count() const { return (document_ ? document_->page_count() : 0); }
    int current_page() const { return page_.page_num; }

    bool in_single_page_view() const;
    bool in_double_page_view() const { return !in_single_page_view(); }

    void get_page(int page_num);

    void refresh();
    void page_up();
    void page_down();
    void change_page(int step);
    void update_status_bar();
    void force_redraw();

    std::shared_ptr<Document> document() const { return document_; }
    void replace_document(std::shared_ptr<Document> document, int page);

    void set_text_annotation_mode(bool enabled);
    void set_performance_mode(PerformanceMode::Mode mode);
    PerformanceMode::Mode performance_mode() const;

signals:
    void annotation_mode_changed(bool enabled);
    void page_changed(int page_num);

protected:

    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void on_page_loaded(std::string name, int page_index);
    void on_annotation_text_finished(const QString &text);
    void on_annotation_text_cancelled();

private:

    QRect calculate_annotation_bounding_box(const Annotation &annotation) const;
    AnnotationHandle find_annotation_at_point(QMouseEvent *event) const;
    void select_annotation(const AnnotationHandle &handle);
    void clear_selection();


    struct ClickTarget {
        int page_num = -1;
        float points_x;
        float points_y;
    };

    ClickTarget get_click_target(QMouseEvent *event) const;


    struct PrefetchEntry {

        PrefetchEntry() = default;
        PrefetchEntry(int page_num, bool double_page, int margin)
            : page_num(page_num)
            , double_page(double_page)
            , border_margin(margin)
        {
        }

        int page_num = -1;

        // render settings
        bool double_page = false;
        //bool zoom_to_content = false;
        int border_margin = 0;

        PixmapPage p1;
        PixmapPage p2;
        QPixmap rendered;

        void clear() { page_num = -1; }
        bool valid(int target_page_num, ConfigFile &config) const;
    };

    struct PrefetchCache {
        PrefetchEntry next;
        PrefetchEntry prev;
    };

    PrefetchCache prefetch_;
    mutable std::mutex prefetch_mutex_;

    void prefetch_async(int page_num);
    QPixmap compose_double_page(const PixmapPage &p1, const PixmapPage &p2) const;
    void clear_prefetch();
    PrefetchEntry make_double_page_entry(int page_num, bool is_current_page) const;
    PrefetchEntry make_single_page_entry(int page_num, bool is_current_page) const;

    void init_ui(int page);
    PixmapPage get_single_page(int page_num);
    PixmapPage get_double_page(int page_num);
    void adjust_initial_subwindow_size();
    void update_scrollbar_visibility();
    void on_scrollbar_value_changed(int new_page);
    void update_image(const QString &message = QString());
    Qt::AlignmentFlag page_alignment() const;

    QLabel *label_;
    StatusBar *status_bar_;
    QScrollBar *scrollbar_;
    QHBoxLayout *layout_;
    std::shared_ptr<Document> document_;
    PageRenderer renderer_;
    ConfigFile *config_;
    PixmapPage page_;
    bool drawing_margin_ = false;
    double aspect_ratio_ = 1.0;
    QRect margin_rect_;
    bool text_annotation_mode_ = false;
    InPlaceAnnotationEditor *annotation_editor_;
    ClickTarget last_click_target_;
    FontInfo annotation_font_;
    AnnotationHandle selected_annotation_;
    bool has_selection_ = false;

    BookmarkPanel *bookmark_panel_ = nullptr;

    // set to true if calling code to set the scrollbar
    // programatically so we don't request a page (the
    // caller will be doing that). Checked in
    // on_scrollbar_value_changed().
    bool manual_scrollbar_change_ = false;
};

