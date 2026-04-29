#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <future>
#include <mutex>
#include <atomic>
#include <memory>
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <QPixmap>
#include <QObject>
#include "page.h"
#include "bookmark.h"
#include "annotation.h"
#include "performance_data.h"

struct fz_context;
struct fz_document;

class Document : public QObject {
    Q_OBJECT
public:
    static int inline unique_id = 0;
    int id;

    Document(std::filesystem::path filename, int dpi, int start_page);
    ~Document();

    void set_is_temporary(bool is_temporary) { is_temporary_ = is_temporary; }
    bool is_temporary() const { return is_temporary_; }

    void kill_load() { kill_loading_ = true; }
    bool is_fully_loaded() const { return get_pending_pages().empty(); }

    std::string filename() const { return filename_.string(); }
    std::filesystem::path path() const { return filename_; }

    int page_count() const { return static_cast<int>(pages_.size()); }
    int dpi() const { return dpi_; }

    Page get_page(int page_num, bool is_current) const;
    bool save();
    void wait_for_save();
    bool is_modified() const { return modified_; }

    std::pair<float, float> get_page_dimensions_points(int page_num) const;

    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    bool bookmarks_file_exists() const;

    bool reparent_bookmark(const BookmarkHandle& handle, const BookmarkHandle& new_parent_handle);
    bool indent_bookmark(const BookmarkHandle& handle);
    bool unindent_bookmark(const BookmarkHandle& handle);
    bool rename_bookmark(const BookmarkHandle& handle, const std::string& title);
    bool remove_bookmark(const BookmarkHandle& handle);

    std::pair<BookmarkHandle, bool> add_bookmark(const std::string& title, int page_num);
    std::pair<BookmarkHandle, bool>
    add_bookmark(const std::string& title, int page_num, const BookmarkHandle& parent_handle);

    // looks for a file named filename.txt in current directory, uses it to set bookmarks
    bool set_bookmarks_from_txt_file();
    bool save_bookmarks_to_txt_file();

    std::vector<Bookmark>& bookmarks() { return bookmarks_; }
    std::vector<Annotation>& annotations() { return annotations_; }

    bool add_annotation(const Annotation& annotation);
    // Place a single SMuFL music glyph (Bravura) as a FreeText annotation with
    // a hand-built appearance stream. baseline_x/y are click coordinates: x in
    // PDF points, y as baseline in PDF Y-up coords.
    bool add_music_symbol_annotation(int page_num, float baseline_x, float baseline_y,
                                     int codepoint, float font_size, int r, int g, int b);
    // Change the font (family/size/color) of one existing annotation. Routes
    // to update_annotation_in_pdf for text annotations and to a delete+re-add
    // for music symbols (where size/color require regenerating the /AP form).
    // The in-memory handle is preserved so callers don't lose the selection.
    bool change_annotation_font(const AnnotationHandle& handle, const FontInfo& new_font);
    bool remove_annotation(const AnnotationHandle& handle);
    bool edit_text_annotation(const AnnotationHandle& handle, const std::string& new_text);
    bool move_annotation(const AnnotationHandle& handle, float new_x, float new_y);
    void move_annotation_in_memory(const AnnotationHandle& handle, float new_x, float new_y);
    bool save_moved_annotation(const AnnotationHandle& handle, float original_x, float original_y);

    // In-memory annotation preview: renders page with a temporary annotation without saving to disk.
    // Returns the rendered page as QImage with the preview annotation overlaid.
    QImage render_page_with_preview_annotation(int page_num, const Annotation& preview_annotation);

    // Render page with an existing annotation shown at a different position (for move preview)
    // original_x/y are needed to find the annotation in the PDF file (before in-memory move)
    QImage render_page_with_moved_annotation(int page_num,
                                             const AnnotationHandle& handle,
                                             float original_x,
                                             float original_y,
                                             float new_x,
                                             float new_y);

    void reload_page(int page_num);


    const PerformanceData& performance_data() const { return performance_data_; }
    PerformanceData& performance_data() { return performance_data_; }

    std::vector<int> get_pending_pages() const;
    void load_page(int page_num);

    // call when tab has focus, so loading is prioritized, preferably BEFORE
    // get_page is called.
    void prioritize() const;

    // Memory management - evict pages far from current position
    void evict_distant_pages(int keep_window_size = 50);

signals:
    void page_loaded(std::string name, int page_index);
    void document_loaded(std::string name, int page);
    void bookmarks_loaded();

private:
    struct PageInfo {
        int page_num;
        float width_points;
        float height_points;
    };

    std::vector<PageInfo> page_info_;

    void initialize_document();
    static std::vector<Annotation> load_annotations_from_pdf(fz_context* ctx, fz_document* doc);
    void reload_annotations();
    BookmarkHandle find_deepest_parent_for_page(int page_num, const std::vector<Bookmark>& bookmarks);
    bool reparent_bookmark(Bookmark bookmark, const BookmarkHandle& new_parent_handle, bool internal_call);
    Bookmark* find_bookmark(const BookmarkHandle& handle);
    Annotation* find_annotation(const AnnotationHandle& handle);

    // Individual PDF annotation operations - each opens PDF, modifies one annotation, saves
    bool add_annotation_to_pdf(Annotation& ann); // Updates ann with final rect from PDF
    bool delete_annotation_from_pdf(const Annotation& ann);
    // preserve_appearance=true skips /AP regeneration so an existing appearance
    // stream (e.g. from Foxit) survives a move. Use false when text/font/color
    // actually changed.
    bool update_annotation_in_pdf(const Annotation& old_ann, const Annotation& new_ann,
                                  bool preserve_appearance = false);
    void clear_completed_features();

    std::vector<Bookmark> bookmarks_;
    std::vector<Annotation> annotations_;
    std::filesystem::path filename_;

    int dpi_;
    int start_page_;
    mutable int current_page_;
    std::vector<Page> pages_;
    bool modified_ = false;
    bool is_temporary_ = false;

    std::mutex save_state_mutex_;
    std::condition_variable save_cv_;
    std::atomic<bool> is_saving_ {false};
    std::atomic<bool> being_destroyed_ {false};

    std::vector<std::vector<Bookmark>> undo_stack_;
    std::vector<std::vector<Bookmark>> redo_stack_;

    std::vector<std::future<void>> save_futures_;

    std::recursive_mutex bookmark_mutex_;
    mutable std::mutex read_mutex_;
    std::atomic<bool> kill_loading_ {false};

    PerformanceData performance_data_;
};