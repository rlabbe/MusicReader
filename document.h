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
    bool is_modified() const { return modified_; }

    std::pair<float, float> get_page_dimensions_points(int page_num) const;

    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    bool reparent_bookmark(const BookmarkHandle& handle, const BookmarkHandle& new_parent_handle);
    bool indent_bookmark(const BookmarkHandle& handle);
    bool unindent_bookmark(const BookmarkHandle& handle);
    bool rename_bookmark(const BookmarkHandle& handle, const std::string& title);
    bool remove_bookmark(const BookmarkHandle& handle);

    std::pair<BookmarkHandle, bool> add_bookmark(const std::string& title, int page_num);
    std::pair<BookmarkHandle, bool> add_bookmark(const std::string& title, int page_num,
                                                 const BookmarkHandle& parent_handle);

    // looks for a file named filename.txt in current directory, uses it to set bookmarks
    bool set_bookmarks_from_txt_file();

    std::vector<Bookmark>& bookmarks() { return bookmarks_; }
    std::vector<Annotation>& annotations() { return annotations_; }

    bool add_annotation(const Annotation& annotation);
    bool remove_annotation(const AnnotationHandle& handle);
    bool edit_text_annotation(const AnnotationHandle& handle, const std::string& new_text);
    bool move_annotation(const AnnotationHandle& handle, float new_x, float new_y);

    void reload_page(int page_num);


    const PerformanceData& performance_data() const { return performance_data_; }
    PerformanceData& performance_data() { return performance_data_; }

    std::vector<int> get_pending_pages() const;
    void load_page(int page_num);

    // call when tab has focus, so loading is prioritized, preferably BEFORE
    // get_page is called.
    void prioritize() const;

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
    BookmarkHandle find_deepest_parent_for_page(int page_num, const std::vector<Bookmark>& bookmarks);
    bool reparent_bookmark(Bookmark bookmark, const BookmarkHandle& new_parent_handle, bool internal_call);
    Bookmark* find_bookmark(const BookmarkHandle& handle);
    Annotation* find_annotation(const AnnotationHandle& handle);
    bool save_annotations_to_pdf();
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