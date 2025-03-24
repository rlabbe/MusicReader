#include "document.h"
#include "logger.h"
#include <windows.h>

#include <assert.h>

#include <format>
#include <stdexcept>
#include <thread>
#include <future>
#include <QImage>
#include <qpainter.h>
#include "qpdf_document.h"


void close_fitz(fz_context *ctx, fz_document *doc)
{
    fz_flush_warnings(ctx);

    if (!ctx) return;
    if (doc)
        fz_drop_document(ctx, doc);
    doc = nullptr;
    if (ctx) fz_drop_context(ctx);
}

std::pair<fz_context *, fz_document *> open_fitz(const std::string &filename)
{
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx)
        return { nullptr, nullptr };

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx); // ensure we can open pdfs
    } fz_catch(ctx)
    {
        close_fitz(ctx, nullptr);
        return { nullptr, nullptr };
    }

    fz_document *doc = nullptr;
    fz_try(ctx)
    {
        doc = fz_open_document(ctx, filename.c_str());
    }
    fz_catch(ctx)
    {
        close_fitz(ctx, doc);
        return { nullptr, nullptr };
    }

    return { ctx, doc };
}



struct PixmapData {
    uchar *data;
    int width;
    int height;
    int stride;
    int size;
    bool success;
};

extern "C" __declspec(noinline) PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi)
{
    PixmapData result = { nullptr, 0, 0, 0, 0, false };
    fz_pixmap *temp_pixmap = nullptr;

    __try {
        fz_matrix transform = fz_scale(dpi / 72.0f, dpi / 72.0f);
        temp_pixmap = fz_new_pixmap_from_page_number(ctx, doc, page_num, transform, fz_device_rgb(ctx), 0);
        if (!temp_pixmap) return result;

        result.width = fz_pixmap_width(ctx, temp_pixmap);
        result.height = fz_pixmap_height(ctx, temp_pixmap);
        result.stride = fz_pixmap_components(ctx, temp_pixmap) * result.width;
        result.size = result.stride * result.height;

        // Allocate memory for a deep copy
        result.data = static_cast<uchar *>(malloc(result.size));
        if (result.data) {
            memcpy(result.data, fz_pixmap_samples(ctx, temp_pixmap), result.size);
            result.success = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        //logger::log_error("Access violation while rendering page " + std::to_string(page_num));
    }

    if (temp_pixmap) fz_drop_pixmap(ctx, temp_pixmap);
    return result;
}

QPixmap render_page(fz_context *ctx, fz_document *doc, int page_num, int dpi)
{
    PixmapData data = render_page_seh(ctx, doc, page_num, dpi);
    if (!data.success) return QPixmap();

    // Create a QImage with copied data
    QImage img(data.data, data.width, data.height, data.stride, QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(img.copy()); // Ensure independent copy

    free(data.data); // Free copied data after QImage is created
    return pixmap;
}


inline bool bookmark_sort(const Bookmark &a, const Bookmark &b)
{
    bool a_is_folder = !a.page_num_.has_value();
    bool b_is_folder = !b.page_num_.has_value();

    if (a_is_folder != b_is_folder) {
        return !a_is_folder;  // Bookmarks with pages come first
    }
    if (!a_is_folder && !b_is_folder) {
        return a.page_num_.value() < b.page_num_.value();  // Compare page numbers
    }
    return false;  // Both are folders, maintain insertion order
}



using namespace std;

Document::Document(std::filesystem::path filename, int dpi, int start_page)
    : filename_(std::move(filename))
    , dpi_(dpi)
    , start_page_(start_page)
{
    int total_pages = 0;

    // Open document once to get page count 

    cerr << "Opening document: " << filename_.string() << endl;

    auto [ctx, doc] = open_fitz(filename_.string());
    cerr << "Opened document: " << filename_.string() << endl;

    if (!ctx || !doc) {
        logger::log_error("Failed to open document: " + filename_.string());
        return;
    }

    cerr << "Getting page count" << endl;
    total_pages = fz_count_pages(ctx, doc);
    cerr << "Got page count: " << total_pages << endl;

    if (total_pages == 0) {
        logger::log_error("Document has no pages: " + filename_.string());
        close_fitz(ctx, doc);
        return;
    }

    // assign page numbers so renderers know what page we are on
    // even before the document is loaded.
    pages_.resize(total_pages);
    for (int i = 0; i < total_pages; ++i)
        pages_[i].page_num = i + 1;


    close_fitz(ctx, doc);
    cerr << "leaving constructor: " << filename_.string() << endl;
}


Document::~Document()
{
    being_destroyed_ = true;

    std::unique_lock<std::mutex> lock(save_state_mutex_);
    save_cv_.wait(lock, [this]() { return !is_saving_; });

    save();
}



void Document::request_page(int page_num) const
{
    std::lock_guard lock(load_order_mutex_);

    load_order_.push_front(page_num - 1);

    // also ask for previous and next; this will more or less
    // let us page forward and backward without waiting for the
    // next page to load.
    if (page_num > 1)
        load_order_.push_front(page_num - 2);

    if (page_num + 1 < page_count())
        load_order_.push_back(page_num);
}


Page Document::get_page(int page_num) const
{
    auto count = page_count();

    if (page_num < 1 || page_num > count) {
        logger::log_error(std::format("Invalid page number: {} for {}",
                                      page_num, filename_.string()));
        if (count == 0)
            return Page(page_num);
        else
            page_num = 1;
    }
    std::lock_guard lock(read_mutex_);
    if (pages_[page_num - 1].is_empty()) {
        // start a new read as soon as we can, sure, it'll be a duplicate, who cares?
        request_page(page_num);
    }
    return pages_[page_num - 1];
}


inline const int NUM_THREADS = std::max(1u, std::thread::hardware_concurrency());
constexpr int MIN_PAGES_PER_THREAD = 1;


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
    cerr << "in load_document: " << filename_.string() << endl;

    fz_context *ctx = nullptr;
    fz_document *doc = nullptr;
    fz_outline *outline = nullptr;

    cerr << "Opening document: " << filename_.string() << endl;
    std::tie(ctx, doc) = open_fitz(filename_.string());
    cerr << "opened" << endl;

    if (!ctx || !doc) {
        logger::log_error("Failed to open document: " + filename_.string());
        return;
    }

    fz_try(ctx)
    {
        cerr << "Getting bookmarks" << endl;
        total_pages = fz_count_pages(ctx, doc);
        cerr << "Got page count: " << total_pages << endl;

        cerr << "Loading bookmarks" << endl;
        outline = fz_load_outline(ctx, doc);
        cerr << "Loaded bookmarks" << endl;

        if (outline) {
            cerr << "Converting bookmarks" << endl;
            bookmarks_ = convert_outline_to_bookmarks(outline);
            cerr << "Converted bookmarks" << endl;
        }
    }
    fz_catch(ctx)
    {
        logger::log_error("MuPDF exception while loading document: " + std::string(fz_caught_message(ctx)));
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

        auto [thread_ctx, thread_doc] = open_fitz(filename_.string());
        if (!thread_ctx || !thread_doc) {
            logger::log_error("Failed to open document in thread for page " + std::to_string(i));
            continue;
        }

        QPixmap pixmap;
        fz_try(thread_ctx)
        {
            pixmap = render_page(thread_ctx, thread_doc, i, dpi_);
        }
        fz_catch(thread_ctx)
        {
            logger::log_error("MuPDF exception rendering page " + std::to_string(i) + ": " + fz_caught_message(thread_ctx));
            close_fitz(thread_ctx, thread_doc);
            continue;
        }

        close_fitz(thread_ctx, thread_doc);

        int page_num = i + 1;
        {
            std::lock_guard lock(read_mutex_);
            pages_[i] = Page(std::move(pixmap), page_num, false);
        }
        emit page_loaded(page_num);
    }
}


void Document::render_page_batch(int start_page,
                                 std::vector<fz_display_list *> &display_lists,
                                 std::vector<fz_rect> &bboxes)
{
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        logger::log_error("Failed to create MuPDF context for rendering thread.");
        return;
    }

    fz_try(ctx)
    {
        for (size_t i = 0; i < display_lists.size(); ++i) {
            if (kill_loading_) break;
            if (!display_lists[i]) continue;

            fz_pixmap *temp_pixmap = nullptr;
            fz_device *dev = nullptr;
            QPixmap pixmap;

            fz_try(ctx)
            {
                fz_matrix transform = fz_scale(dpi_ / 72.0f, dpi_ / 72.0f);
                temp_pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_round_rect(bboxes[i]), nullptr, 0);
                fz_clear_pixmap_with_value(ctx, temp_pixmap, 0xFF);

                dev = fz_new_draw_device(ctx, fz_identity, temp_pixmap);
                fz_run_display_list(ctx, display_lists[i], dev, fz_identity, bboxes[i], nullptr);
                fz_close_device(ctx, dev);

                int width = fz_pixmap_width(ctx, temp_pixmap);
                int height = fz_pixmap_height(ctx, temp_pixmap);
                int stride = fz_pixmap_components(ctx, temp_pixmap) * width;
                const uchar *data = fz_pixmap_samples(ctx, temp_pixmap);

                QImage image(data, width, height, stride, QImage::Format_RGB888);
                pixmap = QPixmap::fromImage(image);
                int page_index = start_page + (int)i;
                int page_num = page_index + 1;
                {
                    std::lock_guard lock(read_mutex_);
                    pages_[page_index] = Page(std::move(pixmap), page_num, false);
                }
                emit page_loaded(page_num);
            }
            fz_always(ctx)
                fz_drop_device(ctx, dev);
            fz_catch(ctx)
            {
                logger::log_error("Failed to render page " + std::to_string(start_page + i));
            }

            if (temp_pixmap)
                fz_drop_pixmap(ctx, temp_pixmap);
        }
    }
    fz_catch(ctx)
    {
        logger::log_error("Exception in rendering thread.");
    }

    fz_drop_context(ctx);
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
        bookmarks_.push_back(bookmark);
        bookmark.parent_handle_.clear();
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
    if (parent_handle) {
        new_bookmark.parent_handle_ = parent_handle;
    }

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

    std::string marks;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
        marks = as_python_list(bookmarks_);
        modified_ = false;
    }

    std::string safe_path = filename_.string();
    std::replace(safe_path.begin(), safe_path.end(), '\\', '/');
    std::string cmd = std::format("set_bookmarks.exe \"{}\" \"{}\"", safe_path, marks);

    int result = std::system(cmd.c_str());

    {
        std::lock_guard<std::mutex> lock(save_state_mutex_);
        is_saving_ = false;
    }
    save_cv_.notify_all();

    return result == 0;
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






/*
bool Document::save(const std::filesystem::path &output_filename, bool block)
{
    // just housekeeping, remove any futures that are done
    clear_completed_features();

    std::lock_guard<std::mutex> lock(bookmark_mutex_);

    std::string filename_to_save = output_filename.empty() ? filename_.string() : output_filename.string();

    auto save_task = [this, filename_to_save]() {
        try {
            auto [ctx, doc] = open_fitz(filename_.string());
            if (!ctx || !doc) {
                logger::log_error("Failed to open document for saving: " + filename_to_save);
                return;
            }

            fz_outline *outline = bookmarks_.empty() ? nullptr : convert_bookmarks_to_outline(bookmarks_);
            fz_set_outline(ctx, doc, outline);
            if (outline) {
                fz_drop_outline(ctx, outline);
            }

            fz_try(ctx)
            {
                if (filename_to_save == filename_.string()) {
                    fz_save_document(ctx, doc, filename_to_save.c_str(), nullptr);
                } else {
                    fz_save_document(ctx, doc, filename_to_save.c_str(), "compress");
                }
            }
            fz_catch(ctx)
            {
                logger::log_error("Error saving document: " + filename_to_save);
            }

            close_fitz(ctx, doc);
            logger::log_info("Document saved: " + filename_to_save);
        } catch (const std::exception &e) {
            logger::log_error("Exception during save: " + std::string(e.what()));
        }
    };


    if (block) {
        save_task();  // Run synchronously
    } else {
        save_futures_.emplace_back(std::async(std::launch::async, save_task));  // Run asynchronously
    }

    return true;
}

*/

/*
TODO reparent_bookmark (add the checks inside the call)


if (auto *bookmark = doc->find_bookmark(handle))
{
    if (auto *new_parent = doc->find_bookmark(target_handle))
    {
        if (doc->reparent_bookmark(*bookmark, target_handle))
        {

  */
