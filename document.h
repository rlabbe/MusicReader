#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <vector>
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


struct fz_context;
struct fz_document;

class Document : public QObject {
    Q_OBJECT
public:
    // you must call load_document() separately, construction
    // only checks for existence and loads # of pages.
    //
    // This facilitates loading the document in a separate thread
    // to keep the UI responsive
    Document(std::filesystem::path filename, int dpi, int start_page);
    ~Document();

    void load_document();

    void kill_load() { kill_loading_ = true; }

    std::string filename() const { return filename_.string(); }
    int page_count() const { return static_cast<int>(pages_.size()); }
    int dpi() const { return dpi_; }

    Page get_page(int page_num) const;
    bool save();
    bool is_modified() const { return modified_; }

    std::pair<float, float> get_page_dimensions_points(int page_num) const;

    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    bool reparent_bookmark(const BookmarkHandle &handle, const BookmarkHandle &new_parent_handle);

    bool indent_bookmark(const BookmarkHandle &handle);
    bool unindent_bookmark(const BookmarkHandle &handle);

    bool rename_bookmark(const BookmarkHandle &handle,
                         const std::string &title);

    bool remove_bookmark(const BookmarkHandle &handle);

    // Creates bookmark; bool is for whether the save worked or not, not
    // whether the bookmark was added
    std::pair<BookmarkHandle, bool> add_bookmark(const std::string &title,
                                                 int page_num);

    std::pair<BookmarkHandle, bool> add_bookmark(const std::string &title,
                                                 int page_num,
                                                 const BookmarkHandle &parent_handle);

    std::vector<Bookmark> &bookmarks() { return bookmarks_; }

    std::vector<Annotation> &annotations() { return annotations_; }

    bool add_annotation(const Annotation &annotation);

    bool remove_annotation(const AnnotationHandle &handle);

    bool edit_text_annotation(const AnnotationHandle &handle, const std::string &new_text);

    bool move_annotation(const AnnotationHandle &handle, float new_x, float new_y);

    void reload_page(int page_num);


signals:
    // emitted when a page is loaded. listen if you want to render while loading is
    // happening
    void page_loaded(int page_index);

    // Notify UI when loading is done
    void document_loaded(std::string name, int page);
    void bookmarks_loaded();

private:

    struct PageInfo {
        int page_num;
        float width_points;
        float height_points;
    };

    std::vector<PageInfo> page_info_;

    static std::vector<Annotation> load_annotations_from_pdf(fz_context *ctx, fz_document *doc);
        

    bool reparent_bookmark(Bookmark bookmark,
                           const BookmarkHandle &new_parent_handle,
                           bool internal_call);

    Bookmark *find_bookmark(const BookmarkHandle &handle);

    Annotation *find_annotation(const AnnotationHandle &handle);
    bool save_annotations_to_pdf();

    void clear_completed_features();

    // User asking for page not yet loaded. This will load the page
    // before any other pages being loaded, and reorder the load order
    // starting here to maximize responsiveness
    void request_page(int page_num) const;

    std::vector<Bookmark> bookmarks_;
    std::vector<Annotation> annotations_;
    std::filesystem::path filename_;

    int dpi_;
    int start_page_;
    std::vector<Page> pages_;
    bool load_started_ = false;
    bool modified_ = false;
    mutable std::mutex load_mutex_;
    std::condition_variable load_cv_;
    std::atomic<bool> loading_done_{ false };

    // prevent race conditions between save and destructor
    std::mutex save_state_mutex_;
    std::condition_variable save_cv_;
    std::atomic<bool> is_saving_{ false };
    std::atomic<bool> being_destroyed_{ false };

    mutable std::list<int> load_order_;
    mutable std::mutex load_order_mutex_;

    std::vector<std::vector<Bookmark>> undo_stack_;
    std::vector<std::vector<Bookmark>> redo_stack_;

    // saves are async for performance, save the futures here
    std::vector<std::future<void>> save_futures_;
    
    std::recursive_mutex bookmark_mutex_;

    mutable std::mutex read_mutex_;

    std::atomic<bool> kill_loading_{ false };
};


