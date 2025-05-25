#define NOMINMAX
#include "document.h"
#include <QGuiApplication>
#include <QScreen>
#include <unordered_set>
#include <qpainter.h>
#include <Windows.h>
#include "logger.h"
#include "fitz_utils.h"
#include "bookmark.h"

#if !defined(NDEBUG)
#pragma warning(push)
#pragma warning( push, 1 )
#include <opencv2/imgproc/imgproc.hpp>
#pragma warning(pop)

#define IF_DEBUG(x) x
#else
#define IF_DEBUG(x)
#endif

#pragma warning(disable : 4611) // disable warning about _setjump not working with c++ destructors


#if !defined(NDEBUG)
cv::Mat qimage_to_mat(QImage img)
{
    cv::Mat mat;
    switch (img.format()) {
    case QImage::Format_RGB888:
        mat = cv::Mat(img.height(), img.width(), CV_8UC3, img.bits(), img.bytesPerLine());
        break;
    case QImage::Format_Grayscale8:
        mat = cv::Mat(img.height(), img.width(), CV_8UC1, img.bits(), img.bytesPerLine());
        break;
    case QImage::Format_Mono:
        mat = cv::Mat(img.height(), img.width(), CV_8UC1, img.bits(), img.bytesPerLine());
        cv::threshold(mat, mat, 128, 255, cv::THRESH_BINARY);
        break;
    case QImage::Format_RGBA8888:
        mat = cv::Mat(img.height(), img.width(), CV_8UC4, img.bits(), img.bytesPerLine());
        break;
    default:
        QImage converted = img.convertToFormat(QImage::Format_RGB888);
        mat = cv::Mat(converted.height(), converted.width(), CV_8UC3, converted.bits(), converted.bytesPerLine());
    }
    return mat.clone();
}
#endif


inline std::string to_string(QImage::Format format)
{
    switch (format) {
    case QImage::Format_Invalid:
        return "Format_Invalid";
    case QImage::Format_Mono:
        return "Format_Mono";
    case QImage::Format_MonoLSB:
        return "Format_MonoLSB";
    case QImage::Format_Indexed8:
        return "Format_Indexed8";
    case QImage::Format_RGB32:
        return "Format_RGB32";
    case QImage::Format_ARGB32:
        return "Format_ARGB32";
    case QImage::Format_ARGB32_Premultiplied:
        return "Format_ARGB32_Premultiplied";
    case QImage::Format_RGB16:
        return "Format_RGB16";
    case QImage::Format_ARGB8565_Premultiplied:
        return "Format_ARGB8565_Premultiplied";
    case QImage::Format_RGB666:
        return "Format_RGB666";
    case QImage::Format_ARGB6666_Premultiplied:
        return "Format_ARGB6666_Premultiplied";
    case QImage::Format_RGB555:
        return "Format_RGB555";
    case QImage::Format_ARGB8555_Premultiplied:
        return "Format_ARGB8555_Premultiplied";
    case QImage::Format_RGB888:
        return "Format_RGB888";
    case QImage::Format_RGB444:
        return "Format_RGB444";
    case QImage::Format_ARGB4444_Premultiplied:
        return "Format_ARGB4444_Premultiplied";
    case QImage::Format_RGBX8888:
        return "Format_RGBX8888";
    case QImage::Format_RGBA8888:
        return "Format_RGBA8888";
    case QImage::Format_RGBA8888_Premultiplied:
        return "Format_RGBA8888_Premultiplied";
    case QImage::Format_BGR30:
        return "Format_BGR30";
    case QImage::Format_A2BGR30_Premultiplied:
        return "Format_A2BGR30_Premultiplied";
    case QImage::Format_RGB30:
        return "Format_RGB30";
    case QImage::Format_A2RGB30_Premultiplied:
        return "Format_A2RGB30_Premultiplied";
    case QImage::Format_Alpha8:
        return "Format_Alpha8";
    case QImage::Format_Grayscale8:
        return "Format_Grayscale8";
    case QImage::Format_RGBX64:
        return "Format_RGBX64";
    case QImage::Format_RGBA64:
        return "Format_RGBA64";
    case QImage::Format_RGBA64_Premultiplied:
        return "Format_RGBA64_Premultiplied";
    case QImage::Format_Grayscale16:
        return "Format_Grayscale16";
    case QImage::Format_BGR888:
        return "Format_BGR888";
    case QImage::Format_RGBX16FPx4:
        return "Format_RGBX16FPx4";
    case QImage::Format_RGBA16FPx4:
        return "Format_RGBA16FPx4";
    case QImage::Format_RGBA16FPx4_Premultiplied:
        return "Format_RGBA16FPx4_Premultiplied";
    case QImage::Format_RGBX32FPx4:
        return "Format_RGBX32FPx4";
    case QImage::Format_RGBA32FPx4:
        return "Format_RGBA32FPx4";
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return "Format_RGBA32FPx4_Premultiplied";
    case QImage::Format_CMYK8888:
        return "Format_CMYK8888";
    default:
        return "Unknown Format (" + std::to_string(static_cast<int>(format)) + ")";
    }
}

inline int get_max_screen_height()
{
    static int max_screen_height = []() {
        int max_height = 0;
        const auto screens = QGuiApplication::screens();
        for (const auto *screen : screens) {
            max_height = std::max(max_height, screen->size().height());
        }
        return max_height;
    }();
    return max_screen_height;
}

inline QImage qimage_from_pixmapdata(const PixmapData &data)
{
    unsigned char *samples = fz_pixmap_samples(data.ctx, data.data);

    // Create initial QImage with the source data
    QImage source_img(samples, data.width, data.height, data.stride, QImage::Format_RGB888);

    // Convert to the detected optimal format if needed
    /*if (format != QImage::Format_RGB888) {
        logger::debug("Converting image format from {} to {}", to_string(source_img.format()), to_string(format));
        return source_img.convertToFormat(format);
    }*/

    // Do not allow image to be taller than the screen height, it slows down
    // rendering for no reason. Also, we need to return a copy of the image because
    // it currently refers to 
    const int max_screen_height = get_max_screen_height();
    if (source_img.height() > max_screen_height) 
        return source_img.scaledToHeight(max_screen_height, Qt::SmoothTransformation);
    else
        return source_img.copy();
}


inline QImage render_page(fz_context *ctx, fz_document *doc, int page_num, int dpi, std::atomic<bool> &quit_now)
{
    PixmapData data = render_page_seh(ctx, doc, page_num, dpi, quit_now);
    if (!data.success) return QImage();

    QImage img = qimage_from_pixmapdata(data);
    fz_drop_pixmap(ctx, data.data);
    return img;
}



Document::Document(std::filesystem::path filename, int dpi, int start_page)
    : filename_(std::move(filename))
    , dpi_(dpi)
    , start_page_(start_page)
{
    int total_pages = 0;

    // Open document once to get page count 

    auto [ctx, doc] = open_fitz(filename_.string());

    if (!ctx || !doc) {
        logger::error("Failed to open document: {}", filename_.string());
        return;
    }

    total_pages = fz_count_pages(ctx, doc);

    if (total_pages == 0) {
        logger::debug("Document has no pages: {}", filename_.string());
        close_fitz(ctx, doc);
        return;
    }

    // assign page numbers so renderers know what page we are on
    // even before the document is loaded.
    pages_.resize(total_pages);
    for (int i = 0; i < total_pages; ++i)
        pages_[i].page_num = i + 1;

    close_fitz(ctx, doc);
}


Document::~Document()
{
    kill_loading_ = true;
    {
        std::unique_lock lock(load_mutex_);
        load_cv_.wait(lock, [this]() { return loading_done_.load(); });
    }

    if (!modified_) return;

    // gotta save it before destroying it. 
    std::unique_lock<std::mutex> lock(save_state_mutex_);
    save_cv_.wait(lock, [this]() { return !is_saving_; });

    save();
    being_destroyed_ = true;
}


void Document::request_page(int page_num) const
{
    std::unordered_set<int> existing;
    existing.reserve(load_order_.size());

    // pages are 1-based, but fitz is 0-based
    int index = page_num - 1;

    {
        std::lock_guard lock(load_order_mutex_);
        if (load_order_.empty()) return;
        existing.insert(load_order_.begin(), load_order_.end());
        load_order_.clear();

        // then reorder rest of requests. get the previous two pages, and then
        //  count forwards from the requested page, and then loop back to page 1.
        // This should maximize the likelihood that a page is loaded based on 
        // typical use patterns (us bookmark to get to some page quickly, then page
        // forward as you play).

        // Start with the requested page
        if (existing.count(index))
            load_order_.push_back(index);

        // may be displaying 2 pages, so ask next page before the previous pages.
        if (existing.count(index + 1))
            load_order_.push_back(index + 1);
    }

    // unlocking load_order_mutex_ should give the load thread a chance to run
    // I'm dubious this matters, but why not?

    {
        std::lock_guard lock(load_order_mutex_);

        // Then previous two pages for fast back page
        if (existing.count(index - 1))
            load_order_.push_back(index - 1);

        if (existing.count(index - 2))
            load_order_.push_back(index - 2);
    }

    {
        std::lock_guard lock(load_order_mutex_);

        // Then all pages after page_num
        for (int i = index + 2; i < page_count(); ++i) {
            if (existing.count(i))
                load_order_.push_back(i);
        }

        // Then all pages before page_num - 2
        for (int i = 0; i < index - 2; ++i) {
            if (existing.count(i))
                load_order_.push_back(i);
        }
    }
}



Page Document::get_page(int page_num) const
{
    auto count = page_count();

    if (page_num < 1 || page_num > count) {
        logger::error(std::format("Invalid page number: {} for {}",
                                      page_num, filename_.string()));
        if (count == 0)
            return Page(page_num);
        else
            page_num = 1;
    }
    /*
    * TODO - not working because we prefetch the next 2 pages, and that causes the initial
    * page to not be loaded immediately because this gets called before the first pages are complete, 
    
    std::lock_guard lock(read_mutex_);
    if (pages_[page_num - 1].is_empty()) {
        // start a new read as soon as we can, sure, it'll be a duplicate, who cares?
        request_page(page_num);
    }*/


    return pages_[page_num - 1];
}


std::list<int> get_page_load_order(int start_page, int total_pages)
{
    std::list<int> load_order;
    load_order.push_back(start_page - 1);
    int start_index = start_page - 1;  // Convert to zero-based index

    // Step 1: Load next page first
    if (start_index + 1 < total_pages) load_order.push_back(start_index + 1);

    // Step 2: Load previous page if it exists
    if (start_index > 0) load_order.push_back(start_index - 1);

    // Step 3: Load remaining pages forward
    for (int i = start_index + 2; i < total_pages; ++i) {
        load_order.push_back(i);
    }

    // Step 4: Load remaining pages backward
    for (int i = start_index - 2; i >= 0; --i) {
        load_order.push_back(i);
    }

    return load_order;
}


void Document::load_document()
{
    if (load_started_) return;
    load_started_ = true;

    int total_pages = page_count();

    // we try to load in order of likely access, but if get_page
    // is called then we will be modifying load_order_ to have it
    // loaded as the very next request.
    load_order_mutex_.lock();
    load_order_ = get_page_load_order(start_page_, total_pages);
    load_order_mutex_.unlock();

    fz_context *ctx = nullptr;
    fz_document *doc = nullptr;
    fz_outline *outline = nullptr;
    std::tie(ctx, doc) = open_fitz(filename_.string());

    if (!ctx || !doc) {
        logger::error("Failed to open document: {}", filename_.string());
        return;
    }

    fz_try(ctx)
    {
        total_pages = fz_count_pages(ctx, doc);
        outline = fz_load_outline(ctx, doc);
        if (outline) {
            bookmarks_ = convert_outline_to_bookmarks(outline);
        }
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception while loading bookmarks{}: {}", filename_.string(), std::string(fz_caught_message(ctx)));
        close_fitz(ctx, doc);
        return;
    }
    close_fitz(ctx, doc);
    emit bookmarks_loaded();

    // simulate very large documents
    //std::this_thread::sleep_for(std::chrono::seconds(5));

    while (true) {
        if (kill_loading_) break;

        load_order_mutex_.lock();
        if (load_order_.empty()) {
            load_order_mutex_.unlock();
            break;
        }

        int i = load_order_.front();
        load_order_.pop_front();
        load_order_mutex_.unlock();

        // may have already been loaded if get_page() was called
        // while this loop was running.
        if (!pages_[i].is_empty()) continue;
        if (kill_loading_) break;

        auto [thread_ctx, thread_doc] = open_fitz(filename_.string());
        if (!thread_ctx || !thread_doc) {
            logger::error("Failed to open document in thread for page {} ", i);
            close_fitz(thread_ctx, thread_doc);
            continue;
        }

        QImage img;
        fz_try(thread_ctx)
        {
            img = render_page(thread_ctx, thread_doc, i, dpi_, kill_loading_);
        }
        fz_catch(thread_ctx)
        {
            logger::error("MuPDF exception rendering page {} : {}", i, fz_caught_message(thread_ctx));
            close_fitz(thread_ctx, thread_doc);
            continue;
        }

        close_fitz(thread_ctx, thread_doc);
        if (kill_loading_) break;

        int page_num = i + 1;
        {
            std::lock_guard lock(read_mutex_);
            pages_[i] = Page(img, page_num, false);
        }
        //logger::debug("{} emitting page_loaded({})", filename_.string(), page_num);
        emit page_loaded(page_num);
    }
    // may have terminated, this just means the function is done.
    // used by destructor
    {
        std::lock_guard lock(load_mutex_);
        loading_done_ = true;
    }
    load_cv_.notify_all();
}



Bookmark *Document::find_bookmark(const BookmarkHandle &handle)
{
    for (auto &bookmark : bookmarks_) {
        if (bookmark.handle_ == handle) return &bookmark;
        auto child = bookmark.find(handle);
        if (child) return child;
    }
    return nullptr;
}


bool Document::reparent_bookmark(const BookmarkHandle &handle, const BookmarkHandle &new_parent_handle)
{
    auto *bookmark = find_bookmark(handle);
    if (!bookmark) return false;
    return reparent_bookmark(*bookmark, new_parent_handle, false);
}


bool Document::reparent_bookmark(Bookmark bookmark, const BookmarkHandle &new_parent_handle, bool internal_call)
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty()) return false;

    if (!internal_call) {
        undo_stack_.push_back(bookmarks_);
    }

    // Remove from current parent if it had one
    if (bookmark.parent_handle_) {
        auto *old_parent = find_bookmark(bookmark.parent_handle_);
        if (old_parent) old_parent->remove_child(bookmark.handle_);
    } else {
        auto it = std::remove_if(bookmarks_.begin(), bookmarks_.end(),
                                 [&](const Bookmark &b) { return b.handle_ == bookmark.handle_; });
        bookmarks_.erase(it, bookmarks_.end());
    }

    // Assign to new parent or move to top level
    if (new_parent_handle) {
        auto *new_parent = find_bookmark(new_parent_handle);
        if (!new_parent) return false;
        new_parent->add_child(bookmark);
        bookmark.parent_handle_ = new_parent_handle;
        std::sort(new_parent->children_.begin(), new_parent->children_.end(), bookmark_sort);
    } else {
        bookmark.parent_handle_.clear();
        bookmarks_.push_back(std::move(bookmark));
        std::sort(bookmarks_.begin(), bookmarks_.end(), bookmark_sort);
    }
    modified_ = true;
    return true;
}


bool Document::indent_bookmark(const BookmarkHandle &handle)
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty()) return false;

    undo_stack_.push_back(bookmarks_);

    auto bookmark = find_bookmark(handle);
    if (!bookmark) return false;

    // make a copy before we start deleting things!
    Bookmark bookmark_copy = *bookmark;

    assert(handle == bookmark->handle_);

    // If the bookmark is already top-level, find its previous sibling
    if (!bookmark->parent_handle_) {
        auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                               [&](const Bookmark &b) { return b.handle_ == handle; });
        if (it == bookmarks_.begin()) return false;  // Cannot indent first item (no previous sibling)

        auto new_parent = std::prev(it);  // Move under previous sibling
        bookmarks_.erase(it);              // Remove from top-level list before reparenting
        return reparent_bookmark(bookmark_copy, new_parent->handle_, true);
    } else {
        // Find the current parent and locate the previous sibling within that parent
        auto parent = find_bookmark(bookmark->parent_handle_);
        if (!parent) return false;  // Parent not found (shouldn't happen)

        auto it = std::find_if(parent->children_.begin(), parent->children_.end(),
                               [&](const Bookmark &b) { return b.handle_ == handle; });
        if (it == parent->children_.begin()) return false;  // Cannot indent first child (no previous sibling)

        auto new_parent = std::prev(it);   // Move under previous sibling
        parent->children_.erase(it);       // Remove from old parent before reparenting
        return reparent_bookmark(bookmark_copy, new_parent->handle_, true);
    }
    return false;
}


bool Document::unindent_bookmark(const BookmarkHandle &handle)
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty()) return false;

    auto bookmark_ptr = find_bookmark(handle);
    if (!bookmark_ptr) return false;

    // make a copy before we start deleting things!
    Bookmark bookmark = *bookmark_ptr;

    if (!bookmark.parent_handle_) return false;  // Already top-level, can't unindent

    undo_stack_.push_back(bookmarks_);

    auto parent = find_bookmark(bookmark.parent_handle_);
    if (!parent) return false;

    // Remove the bookmark from its current parent
    if (!parent->remove_child(handle)) return false;

    // Move to grandparent or top level
    if (parent->parent_handle_) {
        auto grandparent = find_bookmark(parent->parent_handle_);
        if (!grandparent) return false;
        grandparent->add_child(bookmark);
        bookmark.parent_handle_ = parent->parent_handle_;
    } else {
        bookmark.parent_handle_.clear();
        bookmarks_.push_back(bookmark);
        std::sort(bookmarks_.begin(), bookmarks_.end(), bookmark_sort);
    }
    modified_ = true;

    return true;
}


bool Document::rename_bookmark(const BookmarkHandle &handle, const std::string &title)
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty()) return false;

    auto bookmark = find_bookmark(handle);
    if (bookmark && bookmark->title_ != title) {
        undo_stack_.push_back(bookmarks_);
        bookmark->title_ = title;
        modified_ = true;
        return true;
    }
    return false;
}


bool Document::remove_bookmark(const BookmarkHandle &handle)
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty()) return false;

    for (auto it = bookmarks_.begin(); it != bookmarks_.end(); ++it) {
        if (it->handle_ == handle) {
            undo_stack_.push_back(bookmarks_);
            bookmarks_.erase(it);
            modified_ = true;
            return true;
        }
        if (it->remove_child(handle)) {
            undo_stack_.push_back(bookmarks_);
            modified_ = true;
            return true;
        }
    }
    return false;
}


std::pair<BookmarkHandle, bool> Document::add_bookmark(const std::string &title, int page_num)
{
    return add_bookmark(title, page_num, BookmarkHandle());
}


std::pair<BookmarkHandle, bool> Document::add_bookmark(const std::string &title,
                                                       int page_num,
                                                       const BookmarkHandle &parent_handle)
{
    undo_stack_.push_back(bookmarks_);  // Save for undo

    // Treat page_num == 0 as no page number (folder)
    std::optional<int> page_num_opt = (page_num == 0) ? std::nullopt : std::optional<int>(page_num);

    Bookmark new_bookmark(title, page_num_opt.has_value() ? page_num_opt.value() : 0);
    if (parent_handle)
        new_bookmark.parent_handle_ = parent_handle;


    if (!parent_handle) {
        bookmarks_.emplace_back(new_bookmark);
        std::sort(bookmarks_.begin(), bookmarks_.end(), bookmark_sort);
    } else {
        auto parent = find_bookmark(parent_handle);
        if (parent) {
            parent->add_child(new_bookmark);
            std::sort(parent->children_.begin(), parent->children_.end(), bookmark_sort);
        }
    }
    modified_ = true;
    return { new_bookmark.handle_, true };
}


bool Document::save()
{
    // Don't allow save if we're being destroyed
    if (being_destroyed_) return false;

    {
        std::lock_guard<std::mutex> lock(save_state_mutex_);
        if (is_saving_ || !modified_)
            return false;

        is_saving_ = true;
    }

    std::vector<Bookmark> bookmarks_copy;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
        bookmarks_copy = bookmarks_;  // Make a copy to avoid holding the lock during save
        modified_ = false;
    }

    BookmarkResult result = add_bookmarks_to_pdf(filename_.string(), bookmarks_copy);
    bool success = (result == BookmarkResult::Success);

    if (!success) {
        logger::error("Failed to save bookmarks to {}: error code {}",
                     filename_.string(), static_cast<int>(result));
        // Restore modified state if save failed
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
        modified_ = true;
    }

    {
        std::lock_guard<std::mutex> lock(save_state_mutex_);
        is_saving_ = false;
    }
    save_cv_.notify_all();

    return success;
}
void save_annotations(fz_context *, fz_document *)
{
    //TODO
}


void Document::clear_completed_features()
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    // Remove completed futures
    save_futures_.erase(std::remove_if(save_futures_.begin(), save_futures_.end(),
                                       [](std::future<void> &f) {
        return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }),
        save_futures_.end());
}


bool Document::can_undo() const
{
    return !undo_stack_.empty();
}


bool Document::can_redo() const
{
    return !redo_stack_.empty();
}


void Document::undo()
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    if (undo_stack_.empty()) return;

    redo_stack_.push_back(bookmarks_);
    bookmarks_ = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    modified_ = true;
}


void Document::redo()
{
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    if (redo_stack_.empty()) return;

    undo_stack_.push_back(bookmarks_);
    bookmarks_ = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    modified_ = true;
}




