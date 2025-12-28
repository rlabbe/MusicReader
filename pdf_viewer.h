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
    PDFViewer(std::shared_ptr<Document> document, ConfigFile* config, int page, StatusBar* sbar, QWidget* parent,
              MusicReader* reader, BookmarkPanel* panel);

    ~PDFViewer();


    // call if the application is closing - this object may receive page_loaded events
    // after the close event, and we want to ignore them. 
    void closing() { closing_ = true; }

    int page_count() const { return renderer_.page_count(); }
    int current_page() const { return page_.page_num; }
    int current_index() const { return renderer_.current_index(); }
    std::string current_page_display() const { return renderer_.current_page_display(); }
    std::vector<std::string> get_page_displays() const { return renderer_.get_all_page_displays(); }

    bool in_single_page_view() const;
    bool in_double_page_view() const { return !in_single_page_view(); }

    void get_page(int page_num);
    void goto_physical_page(int physical_page);

    void refresh();
    void refresh_if_dirty();
    void mark_dirty();

    void page_up();
    void page_down();
    void change_page(int step);
    void update_status_bar();
    void force_redraw();

    std::shared_ptr<Document> document() const { return document_; }
    void replace_document(std::shared_ptr<Document> document, int page);

    void set_text_annotation_mode(bool enabled);
    void set_page_break_edit_mode(bool enabled);
    void set_performance_mode(PerformanceMode::Mode mode);
    PerformanceMode::Mode performance_mode() const;
    bool in_page_break_edit_mode() const { return page_break_edit_mode_; }

signals:
    void annotation_mode_changed(bool enabled);
    void page_break_edit_mode_changed(bool enabled);
    void page_changed(int page_num);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void on_page_loaded(std::string name, int page_index);
    void on_annotation_text_finished(const QString& text);
    void on_annotation_text_cancelled();
    void delete_shortcut();

private:
    QRect calculate_annotation_bounding_box(const Annotation& annotation) const;
    AnnotationHandle find_annotation_at_point(QMouseEvent* event) const;
    void select_annotation(const AnnotationHandle& handle);
    void clear_selection();


    struct ClickTarget {
        int page_num = -1;
        float points_x = 0;
        float points_y = 0;
    };

    ClickTarget get_click_target(QMouseEvent* event) const;


    struct PrefetchEntry {

        PrefetchEntry() = default;
        PrefetchEntry(int index, bool double_page, int margin)
            : index(index)
            , double_page(double_page)
            , border_margin(margin)
        {
        }

        int index = -1;

        // render settings
        bool double_page = false;
        // bool zoom_to_content = false;
        int border_margin = 0;

        PixmapPage p1;
        PixmapPage p2;
        QPixmap rendered;

        void clear() { index = -1; }
        bool valid(int target_index, ConfigFile& config) const;
    };

    struct PrefetchCache {
        PrefetchEntry next;
        PrefetchEntry prev;
    };

    PrefetchCache prefetch_;
    mutable std::mutex prefetch_mutex_;

    void prefetch_async(int index);
    QPixmap compose_double_page(const PixmapPage& p1, const PixmapPage& p2) const;
    void clear_prefetch();
    PrefetchEntry make_double_page_entry(int index) const;
    PrefetchEntry make_single_page_entry(int index) const;

    void init_ui(int page);
    void adjust_initial_subwindow_size();
    void update_scrollbar_visibility();
    void on_scrollbar_value_changed(int new_page);

    // Convert normalized position (0.0-1.0 relative to full page) to display Y coordinate
    // Accounts for zoom-to-content cropping

    int normalized_to_display_y(double normalized_pos, int display_height) const;

    // Convert display Y coordinate to normalized position (0.0-1.0 relative to full page)
    // Accounts for zoom-to-content cropping
    double display_y_to_normalized(int display_y, int display_height) const;
    void update_image(const QString& message = QString());
    Qt::AlignmentFlag page_alignment() const;
    Qt::AlignmentFlag page_vertical_alignment() const;

    QLabel* label_;
    StatusBar* status_bar_;
    QScrollBar* scrollbar_;
    QHBoxLayout* layout_;
    std::shared_ptr<Document> document_;
    PageRenderer renderer_;
    ConfigFile* config_;
    PixmapPage page_;
    bool drawing_margin_ = false;
    double aspect_ratio_ = 1.0;
    QRect margin_rect_;
    bool text_annotation_mode_ = false;
    bool page_break_edit_mode_ = false;
    bool dragging_page_break_ = false;
    double dragging_break_position_ = 0.0;
    double original_break_position_ = 0.0;
    bool dragging_existing_break_ = false;
    InPlaceAnnotationEditor* annotation_editor_;
    ClickTarget last_click_target_;
    FontInfo annotation_font_;
    AnnotationHandle selected_annotation_;
    bool has_selection_ = false;
    bool skip_resize_update_ = false;
    bool closing_ = false;
    bool dirty_ = false;

    BookmarkPanel* bookmark_panel_ = nullptr;

    // set to true if calling code to set the scrollbar
    // programatically so we don't request a page (the
    // caller will be doing that). Checked in
    // on_scrollbar_value_changed().
    bool manual_scrollbar_change_ = false;
};
