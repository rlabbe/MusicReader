#include "document.h"
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <QFontMetricsF>
#include <unordered_set>
#include <algorithm>
#include <map>
#include <qpainter.h>
#include <Windows.h>
#include <shlobj.h>
#include "logger.h"
#include "fitz_utils.h"
#include "annotation_coords.h"
#include "bookmark.h"
#include "document_load_manager.h"
#pragma warning(push, 1)
#include <mupdf/pdf.h>
#pragma warning(pop)
#include "document_helpers.h"
#include "exception_logger.h"
#include <fstream>
#include <qmessagebox.h>
#include "config_file.h"


Document::Document(std::filesystem::path filename, int dpi, int start_page)
    : filename_(std::move(filename))
    , dpi_(dpi)
    , start_page_(start_page)
    , current_page_(start_page)
{
    ++unique_id;
    id = unique_id;

    TRACE_FUNCTION_MSG("document({})", id);

    initialize_document();
}


Document::~Document()
{
    TRACE_FUNCTION_MSG("document({})", id);

    kill_loading_ = true;

    if (is_temporary()) {
        std::filesystem::remove(filename_);
        return;
    }

    if (!modified_)
        return;

    // Wait for any in-progress save to complete before we save final state
    {
        std::unique_lock<std::mutex> lock(save_state_mutex_);
        save_cv_.wait(lock, [this]() {
            return !is_saving_;
        });
        being_destroyed_ = true;
    }
    // Lock released before save() to avoid deadlock (save() also acquires save_state_mutex_)

    // Do final save directly here since save() checks being_destroyed_ and would skip
    std::vector<Bookmark> bookmarks_copy;
    std::vector<Annotation> annotations_copy;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
        bookmarks_copy = bookmarks_;
        annotations_copy = annotations_;
    }

    add_bookmarks_to_pdf(filename_.string(), bookmarks_copy);
    save_annotations_to_pdf();
    performance_data_.save(filename_);
}


Page Document::get_page(int page_num, bool is_current) const
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("Document({}) Requesting page {} of {}, is_current={}", id, page_num, filename_.string(),
                       is_current);

    if (is_current)
        current_page_ = page_num;

    auto count = page_count();

    if (page_num < 1 || page_num > count) {
        logger::error(std::format("Invalid page number: {} for {}", page_num, filename_.string()));
        if (count == 0)
            return Page(page_num);
        else
            page_num = 1;
    }
    if (is_current) {
        bool need_to_request = false;
        {
            std::lock_guard lock(read_mutex_);
            if (pages_[page_num - 1].is_empty())
                need_to_request = true;
        }
        if (need_to_request)
            prioritize();
    }

    return pages_[page_num - 1];
}


void Document::prioritize() const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (auto* manager = DocumentLoadManager::instance())
        manager->prioritize_page(filename_);
}


void Document::initialize_document()
{
    SAFE_METHOD;
    TRACE_FUNCTION_MSG("Document({}) Initializing", id);

    modified_ = false;

    auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc) {
        logger::error("Failed to open document({}): {}", id, filename_.string());
        return;
    }

    int total_pages = 0;
    fz_outline* outline = nullptr;

    fz_try(ctx)
    {
        total_pages = fz_count_pages(ctx, doc);
        outline = fz_load_outline(ctx, doc);
        if (outline)
            bookmarks_ = convert_outline_to_bookmarks(outline);
        annotations_ = load_annotations_from_pdf(ctx, doc);
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception while loading bookmarks{}: {}", filename_.string(),
                      std::string(fz_caught_message(ctx)));
        bookmarks_.clear();
        modified_ = true; // this will make it save the bookmarks changes at the end of this function
    }

    if (total_pages == 0) {
        logger::debug("Document({}) has no pages: {}", id, filename_.string());
        close_fitz(ctx, doc);
        return;
    }

    pages_.resize(total_pages);
    for (int i = 0; i < total_pages; ++i)
        pages_[i].page_num = i + 1;

    page_info_.resize(total_pages);
    for (int i = 0; i < total_pages; ++i) {
        fz_page* page = nullptr;
        fz_try(ctx)
        {
            page = fz_load_page(ctx, doc, i);
            fz_rect bounds = fz_bound_page(ctx, page);
            page_info_[i] = {i + 1, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0};
            fz_drop_page(ctx, page);
        }
        fz_catch(ctx)
        {
            if (page)
                fz_drop_page(ctx, page);
            page_info_[i] = {i + 1, 612.0f, 792.0f}; // Default letter size
        }
    }

    close_fitz(ctx, doc);

    // Load performance data from .perf file if it exists
    performance_data_.load(filename_);

    // If we got an exception loading bookmarks, we want to save the document without them.
    if (modified_)
        save();

    emit bookmarks_loaded();
}


std::vector<int> get_page_load_order(int start_page, int total_pages, const std::string& doc_name)
{
    SAFE_METHOD;
    TRACE_CALL_MSG("{}", doc_name);

    std::vector<int> load_order;
    load_order.push_back(start_page);

    // Step 1: Load next page first
    if (start_page + 1 <= total_pages)
        load_order.push_back(start_page + 1);

    // Step 2: Load previous page if it exists
    if (start_page > 0)
        load_order.push_back(start_page - 1);

    // Step 3: Load remaining pages forward (start from +2 to avoid duplicate)
    for (int i = start_page + 2; i <= total_pages; ++i)
        load_order.push_back(i);

    // Step 4: Load remaining pages backward (start from -2 to avoid duplicate)
    for (int i = start_page - 2; i > 0; --i)
        load_order.push_back(i);

    return load_order;
}


std::vector<int> Document::get_pending_pages() const
{
    std::lock_guard<std::mutex> lock(read_mutex_);

    // Get all pending pages
    std::vector<int> pending;
    for (int i = 0; i < page_count(); ++i) {
        if (pages_[i].is_empty())
            pending.push_back(i + 1);
    }

    if (pending.empty())
        return pending;

    // Convert to set for fast lookups
    std::unordered_set<int> pending_set(pending.begin(), pending.end());

    // Get load order starting from start_page
    auto load_order = get_page_load_order(current_page_, page_count(), filename());

    // Return pending pages in load order
    std::vector<int> ordered_pending;
    for (int page : load_order) {
        if (pending_set.contains(page))
            ordered_pending.push_back(page);
    }

    // Sort and check for duplicates
    std::vector<int> sorted_result = ordered_pending;
    std::sort(sorted_result.begin(), sorted_result.end());

    // Check for duplicates - set breakpoint here
    for (size_t i = 1; i < sorted_result.size(); ++i) {
        if (sorted_result[i] == sorted_result[i - 1]) {
            // Duplicate found - breakpoint here
            int duplicate_page = sorted_result[i];
            (void)duplicate_page; // Prevent unused variable warning
        }
    }

    return ordered_pending;
}


void Document::load_page(int page_num)
{
    SAFE_METHOD;
    TRACE_CALL_MSG("doc {} file: {} page: {}", id, filename_.string(), page_num);

    if (kill_loading_ || page_num < 1 || page_num > page_count())
        return;

    {
        std::lock_guard lock(read_mutex_);
        if (!pages_[page_num - 1].is_empty())
            return;
    }

    auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc)
        return;

    QImage img;
    fz_try(ctx)
    {
        img = render_page(ctx, doc, page_num - 1, dpi_, kill_loading_);
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception loading page {}: {}", page_num, fz_caught_message(ctx));
        close_fitz(ctx, doc);
        return;
    }

    close_fitz(ctx, doc);

    if (kill_loading_)
        return;

    int sleep = ConfigFile::instance().page_load_delay();
    if (sleep > 0) {
        static bool warned = false;
        if (!warned) {
            logger::info("Page load delay of {} ms enabled", sleep);
            warned = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
    }


    {
        std::lock_guard lock(read_mutex_);
        pages_[page_num - 1] = Page(img, page_num, false);
    }

    logger::debug("emitting page_loaded {} {}", filename_.string(), page_num);
    emit page_loaded(filename_.string(), page_num);
}


std::pair<float, float> Document::get_page_dimensions_points(int page_num) const
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (page_num < 1 || page_num > static_cast<int>(page_info_.size()))
        return {0.0f, 0.0f};

    const auto& info = page_info_[page_num - 1];
    return {info.width_points, info.height_points};
}


Bookmark* Document::find_bookmark(const BookmarkHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    for (auto& bookmark : bookmarks_) {
        if (bookmark.handle_ == handle)
            return &bookmark;
        auto child = bookmark.find(handle);
        if (child)
            return child;
    }
    return nullptr;
}


bool Document::reparent_bookmark(const BookmarkHandle& handle, const BookmarkHandle& new_parent_handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto* bookmark = find_bookmark(handle);
    if (!bookmark)
        return false;
    return reparent_bookmark(*bookmark, new_parent_handle, false);
}


bool Document::reparent_bookmark(Bookmark bookmark, const BookmarkHandle& new_parent_handle, bool internal_call)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty())
        return false;

    if (!internal_call) {
        undo_stack_.push_back(bookmarks_);
    }

    // Remove from current parent if it had one
    if (bookmark.parent_handle_) {
        auto* old_parent = find_bookmark(bookmark.parent_handle_);
        if (old_parent)
            old_parent->remove_child(bookmark.handle_);
    } else {
        auto it = std::remove_if(bookmarks_.begin(), bookmarks_.end(), [&](const Bookmark& b) {
            return b.handle_ == bookmark.handle_;
        });
        bookmarks_.erase(it, bookmarks_.end());
    }

    // Assign to new parent or move to top level
    if (new_parent_handle) {
        auto* new_parent = find_bookmark(new_parent_handle);
        if (!new_parent)
            return false;
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


bool Document::indent_bookmark(const BookmarkHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty())
        return false;

    undo_stack_.push_back(bookmarks_);

    auto bookmark = find_bookmark(handle);
    if (!bookmark)
        return false;

    // make a copy before we start deleting things!
    Bookmark bookmark_copy = *bookmark;

    assert(handle == bookmark->handle_);

    // If the bookmark is already top-level, find its previous sibling
    if (!bookmark->parent_handle_) {
        auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(), [&](const Bookmark& b) {
            return b.handle_ == handle;
        });
        if (it == bookmarks_.begin())
            return false; // Cannot indent first item (no previous sibling)

        auto new_parent = std::prev(it); // Move under previous sibling
        bookmarks_.erase(it);            // Remove from top-level list before reparenting
        return reparent_bookmark(bookmark_copy, new_parent->handle_, true);
    } else {
        // Find the current parent and locate the previous sibling within that parent
        auto parent = find_bookmark(bookmark->parent_handle_);
        if (!parent)
            return false; // Parent not found (shouldn't happen)

        auto it = std::find_if(parent->children_.begin(), parent->children_.end(), [&](const Bookmark& b) {
            return b.handle_ == handle;
        });
        if (it == parent->children_.begin())
            return false; // Cannot indent first child (no previous sibling)

        auto new_parent = std::prev(it); // Move under previous sibling
        parent->children_.erase(it);     // Remove from old parent before reparenting
        return reparent_bookmark(bookmark_copy, new_parent->handle_, true);
    }
    return false;
}


bool Document::unindent_bookmark(const BookmarkHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty())
        return false;

    auto bookmark_ptr = find_bookmark(handle);
    if (!bookmark_ptr)
        return false;

    // make a copy before we start deleting things!
    Bookmark bookmark = *bookmark_ptr;

    if (!bookmark.parent_handle_)
        return false; // Already top-level, can't unindent

    undo_stack_.push_back(bookmarks_);

    auto parent = find_bookmark(bookmark.parent_handle_);
    if (!parent)
        return false;

    // Remove the bookmark from its current parent
    if (!parent->remove_child(handle))
        return false;

    // Unindent means: become a sibling of your current parent
    // So new parent is your current parent's parent
    BookmarkHandle new_parent_handle = parent->parent_handle_;
    bookmark.parent_handle_ = new_parent_handle;

    if (new_parent_handle) {
        // Insert as child of grandparent, right after current parent
        auto* grandparent = find_bookmark(new_parent_handle);
        if (!grandparent)
            return false;

        auto parent_it =
            std::find_if(grandparent->children_.begin(), grandparent->children_.end(), [&](const Bookmark& b) {
                return b.handle_ == parent->handle_;
            });

        if (parent_it != grandparent->children_.end())
            grandparent->children_.insert(parent_it + 1, bookmark);
        else
            grandparent->children_.push_back(bookmark);
    } else {
        // Parent was top-level, so insert at top level right after parent
        auto parent_it = std::find_if(bookmarks_.begin(), bookmarks_.end(), [&](const Bookmark& b) {
            return b.handle_ == parent->handle_;
        });

        if (parent_it != bookmarks_.end())
            bookmarks_.insert(parent_it + 1, bookmark);
        else
            bookmarks_.push_back(bookmark);
    }

    modified_ = true;
    return true;
}

bool Document::rename_bookmark(const BookmarkHandle& handle, const std::string& title)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty())
        return false;

    auto bookmark = find_bookmark(handle);
    if (bookmark && bookmark->title_ != title) {
        undo_stack_.push_back(bookmarks_);
        bookmark->title_ = title;
        modified_ = true;
        return true;
    }
    return false;
}


bool Document::remove_bookmark(const BookmarkHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    if (bookmarks_.empty())
        return false;

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


std::pair<BookmarkHandle, bool> Document::add_bookmark(const std::string& title, int page_num)
{
    return add_bookmark(title, page_num, BookmarkHandle());
}


std::pair<BookmarkHandle, bool> Document::add_bookmark(const std::string& title, int page_num,
                                                       const BookmarkHandle& parent_handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    undo_stack_.push_back(bookmarks_);

    Bookmark new_bookmark(title, page_num);

    if (parent_handle) {
        // If explicit parent specified, just add there
        new_bookmark.parent_handle_ = parent_handle;
        auto parent = find_bookmark(parent_handle);
        if (parent) {
            auto insert_pos =
                std::upper_bound(parent->children_.begin(), parent->children_.end(), new_bookmark, bookmark_sort);
            parent->children_.insert(insert_pos, new_bookmark);
        }
    } else {
        // Find deepest appropriate parent in hierarchy
        BookmarkHandle best_parent = find_deepest_parent_for_page(page_num, bookmarks_);

        if (best_parent) {
            new_bookmark.parent_handle_ = best_parent;
            auto parent = find_bookmark(best_parent);
            auto insert_pos =
                std::upper_bound(parent->children_.begin(), parent->children_.end(), new_bookmark, bookmark_sort);
            parent->children_.insert(insert_pos, new_bookmark);
        } else {
            // Add to top level
            auto insert_pos = std::upper_bound(bookmarks_.begin(), bookmarks_.end(), new_bookmark, bookmark_sort);
            bookmarks_.insert(insert_pos, new_bookmark);
        }
    }

    modified_ = true;
    return {new_bookmark.handle_, true};
}


BookmarkHandle Document::find_deepest_parent_for_page(int page_num, const std::vector<Bookmark>& bookmarks)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    // Flatten all bookmarks into page order with their parents
    std::vector<std::pair<int, BookmarkHandle>> flattened;
    flatten_bookmarks(bookmarks, BookmarkHandle(), flattened);

    // Find where page_num fits in the sequence
    for (const auto& [page, parent] : flattened) {
        if (page >= page_num) {
            // Found first bookmark with page >= page_num
            // New bookmark should have same parent
            return parent;
        }
    }

    // Page number is higher than all existing bookmarks
    // Use same parent as last bookmark, or top level if empty
    if (!flattened.empty())
        return flattened.back().second;

    return BookmarkHandle(); // Top level
}


bool Document::save()
{
    SAFE_METHOD;

    // Don't allow save if we're being destroyed or not modified
    if (being_destroyed_ || !modified_)
        return false;

    {
        std::lock_guard<std::mutex> lock(save_state_mutex_);
        if (is_saving_ || !modified_)
            return false;
        is_saving_ = true;
    }
    TRACE_FUNCTION;

    std::vector<Bookmark> bookmarks_copy;
    std::vector<Annotation> annotations_copy;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
        bookmarks_copy = bookmarks_;
        annotations_copy = annotations_;
        modified_ = false;
    }

    BookmarkResult bookmark_result = add_bookmarks_to_pdf(filename_.string(), bookmarks_copy);
    bool success = (bookmark_result == BookmarkResult::Success);

    if (success) {
        success = save_annotations_to_pdf();
        if (!success)
            logger::error("Failed to save annotations to {}", filename_.string());
    } else {
        logger::error("Failed to save bookmarks to {}: error code {}", filename_.string(),
                      static_cast<int>(bookmark_result));
    }

    // Save performance data (independent of PDF save success)
    performance_data_.save(filename_);

    if (!success) {
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


void Document::wait_for_save()
{
    std::unique_lock<std::mutex> lock(save_state_mutex_);
    save_cv_.wait(lock, [this]() {
        return !is_saving_;
    });
}


void Document::clear_completed_features()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    // Remove completed futures
    save_futures_.erase(std::remove_if(save_futures_.begin(), save_futures_.end(),
                                       [](std::future<void>& f) {
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
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    if (undo_stack_.empty())
        return;

    redo_stack_.push_back(bookmarks_);
    bookmarks_ = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    modified_ = true;
}


void Document::redo()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    if (redo_stack_.empty())
        return;

    undo_stack_.push_back(bookmarks_);
    bookmarks_ = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    modified_ = true;
}


Annotation* Document::find_annotation(const AnnotationHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;


    for (auto& annotation : annotations_) {
        if (annotation.handle_ == handle)
            return &annotation;
    }
    return nullptr;
}

bool Document::add_annotation(const Annotation& annotation)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    int page_num = annotation.page_num_;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
        annotations_.push_back(annotation);
        modified_ = true;
    }
    // Lock released before save() to avoid deadlock with save_state_mutex_

    bool save_success = save();

    // Reload annotations from PDF to get correct rect coordinates
    // (click position gets transformed to proper rect on save)
    reload_annotations();

    reload_page(page_num);
    return save_success;
}

bool Document::remove_annotation(const AnnotationHandle& handle)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    int page_num = -1;
    bool found = false;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

        // Find the annotation first to get its page number before removal
        for (const auto& a : annotations_) {
            if (a.handle_ == handle) {
                page_num = a.page_num_;
                break;
            }
        }

        if (page_num < 0)
            return false;

        auto it = std::remove_if(annotations_.begin(), annotations_.end(), [&](const Annotation& a) {
            return a.handle_ == handle;
        });
        if (it != annotations_.end()) {
            annotations_.erase(it, annotations_.end());
            modified_ = true;
            found = true;
        }
    }
    // Lock released before save() to avoid deadlock with save_state_mutex_

    if (found) {
        bool save_success = save();
        reload_page(page_num);
        return save_success;
    }
    logger::error("remove_annotation: annotation not found with handle {}", static_cast<int>(handle));
    return false;
}

bool Document::edit_text_annotation(const AnnotationHandle& handle, const std::string& new_text)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    int page_num = -1;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

        auto* annotation = find_annotation(handle);
        if (annotation) {
            annotation->text_ = new_text;
            modified_ = true;
            page_num = annotation->page_num_;
        }
    }
    // Lock released before save() to avoid deadlock with save_state_mutex_

    if (page_num > 0) {
        bool save_success = save();
        reload_page(page_num);
        return save_success;
    }
    return false;
}


bool Document::move_annotation(const AnnotationHandle& handle, float new_x, float new_y)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    int page_num = -1;
    {
        std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

        auto* annotation = find_annotation(handle);
        if (annotation) {
            annotation->x_ = new_x;
            annotation->y_ = new_y;
            modified_ = true;
            page_num = annotation->page_num_;
        }
    }
    // Lock released before save() to avoid deadlock with save_state_mutex_

    if (page_num > 0) {
        bool save_success = save();
        reload_page(page_num);
        return save_success;
    }
    return false;
}


void Document::reload_page(int page_num)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (page_num < 1 || page_num > page_count())
        return;

    auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc)
        return;

    QImage img;
    fz_try(ctx)
    {
        img = render_page(ctx, doc, page_num - 1, dpi_, kill_loading_);
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception reloading page {}: {}", page_num, fz_caught_message(ctx));
        close_fitz(ctx, doc);
        return;
    }

    close_fitz(ctx, doc);

    {
        std::lock_guard lock(read_mutex_);
        pages_[page_num - 1] = Page(img, page_num, false);
    }
    logger::debug("emiting page_loader {} {}", filename_.string(), page_num);
    emit page_loaded(filename_.string(), page_num);
}


QImage Document::render_page_with_preview_annotation(int page_num, const Annotation& preview_annotation)
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (page_num < 1 || page_num > page_count())
        return QImage();

    auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc)
        return QImage();

    pdf_document* pdf = pdf_specifics(ctx, doc);
    if (!pdf) {
        close_fitz(ctx, doc);
        return QImage();
    }

    QImage result;
    pdf_page* page = nullptr;
    fz_try(ctx)
    {
        page = pdf_load_page(ctx, pdf, page_num - 1);
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception loading page {} for preview: {}", page_num, fz_caught_message(ctx));
        close_fitz(ctx, doc);
        return QImage();
    }

    if (!page) {
        close_fitz(ctx, doc);
        return QImage();
    }

    fz_try(ctx)
    {
        // Get page bounds for coordinate conversion
        fz_rect page_bounds = fz_bound_page(ctx, (fz_page*)page);
        float page_height = page_bounds.y1 - page_bounds.y0;

        // Create a temporary FreeText annotation in memory
        pdf_annot* annot = pdf_create_annot(ctx, page, PDF_ANNOT_FREE_TEXT);

        // Build annotation rect from baseline position
        AnnotationCoordinates coords;
        coords.set_context({page_height, page_bounds.x1 - page_bounds.x0, 0, 0}); // display dims unused here
        float font_size = preview_annotation.font_info_.size;
        float rect_y0 = coords.baseline_to_rect_top_screen(preview_annotation.y_, font_size);
        float rect_y1 = rect_y0 + preview_annotation.height_;
        fz_rect rect =
            fz_make_rect(preview_annotation.x_, rect_y0, preview_annotation.x_ + preview_annotation.width_, rect_y1);
        pdf_set_annot_rect(ctx, annot, rect);

        // Set content
        pdf_set_annot_contents(ctx, annot, preview_annotation.text_.c_str());

        // Set default appearance (font and color)
        auto& font = preview_annotation.font_info_;
        std::string mupdf_font_name = pdf_font_to_mupdf_font(font.family);
        auto [cr, cg, cb] = font.color;
        float color[3] = {cr / 255.0f, cg / 255.0f, cb / 255.0f};
        pdf_set_annot_default_appearance(ctx, annot, mupdf_font_name.c_str(), font.size, 3, color);

        // Set alignment and border
        pdf_set_annot_quadding(ctx, annot, 0);
        pdf_set_annot_border(ctx, annot, 0);

        // Generate appearance stream so it renders properly
        pdf_update_annot(ctx, annot);

        // Now render the page (including the in-memory annotation)
        fz_matrix transform = fz_scale(dpi_ / 72.0f, dpi_ / 72.0f);
        fz_pixmap* pixmap = fz_new_pixmap_from_page(ctx, (fz_page*)page, transform, fz_device_rgb(ctx), 0);

        if (pixmap) {
            int width = fz_pixmap_width(ctx, pixmap);
            int height = fz_pixmap_height(ctx, pixmap);
            unsigned char* samples = fz_pixmap_samples(ctx, pixmap);
            int stride = fz_pixmap_stride(ctx, pixmap);

            QImage img(samples, width, height, stride, QImage::Format_RGB888);
            result = img.copy();

            fz_drop_pixmap(ctx, pixmap);
        }

        // Clean up - delete the temporary annotation before closing
        pdf_delete_annot(ctx, page, annot);
        pdf_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception rendering preview annotation on page {}: {}", page_num, fz_caught_message(ctx));
        if (page)
            pdf_drop_page(ctx, page);
    }

    close_fitz(ctx, doc);
    return result;
}


std::vector<Annotation> Document::load_annotations_from_pdf(fz_context* ctx, fz_document* doc)
{
    SAFE_METHOD;
    TRACE_CALL;

    std::vector<Annotation> annotations;
    if (!ctx || !doc)
        return annotations;

    pdf_document* pdf = pdf_specifics(ctx, doc);
    if (!pdf)
        return annotations;

    fz_try(ctx)
    {
        int page_count = fz_count_pages(ctx, doc);

        for (int page_idx = 0; page_idx < page_count; ++page_idx) {
            pdf_page* page = pdf_load_page(ctx, pdf, page_idx);
            if (!page)
                continue;

            // Use pdf_first_annot/pdf_next_annot to get only CURRENT annotations
            // (properly handles incremental saves and xref resolution)
            pdf_annot* annot = pdf_first_annot(ctx, page);
            while (annot) {
                if (pdf_annot_type(ctx, annot) == PDF_ANNOT_FREE_TEXT) {
                    // Get annotation object from the annot handle
                    pdf_obj* annot_obj = pdf_annot_obj(ctx, annot);

                    // Extract annotation properties
                    pdf_obj* rect = pdf_dict_get(ctx, annot_obj, PDF_NAME(Rect));
                    pdf_obj* contents = pdf_dict_get(ctx, annot_obj, PDF_NAME(Contents));

                    if (rect && contents) {
                        // PDF rect format: [x0, y0, x1, y1] = [left, bottom, right, top]
                        float x0 = pdf_array_get_real(ctx, rect, 0); // left
                        float y0 = pdf_array_get_real(ctx, rect, 1); // bottom
                        float x1 = pdf_array_get_real(ctx, rect, 2); // right
                        float y1 = pdf_array_get_real(ctx, rect, 3); // top

                        float x = x0;           // left edge
                        float width = x1 - x0;  // right - left
                        float height = y1 - y0; // top - bottom
                        const char* text = pdf_to_text_string(ctx, contents);

                        // Extract font, size, and color from default appearance
                        std::string font_name;
                        float font_size = 12.0f;
                        int r = 0, g = 0, b = 0;

                        // Parse DA (Default Appearance) string
                        pdf_obj* da = pdf_dict_get(ctx, annot_obj, PDF_NAME(DA));
                        if (da) {
                            const char* da_str = pdf_to_text_string(ctx, da);
                            if (da_str) {
                                // Parse DA string format: "/FontName FontSize Tf r g b rg"
                                std::string da_string(da_str);

                                // Extract font name (starts with /)
                                size_t font_start = da_string.find('/');
                                if (font_start != std::string::npos) {
                                    size_t font_end = da_string.find(' ', font_start);
                                    if (font_end != std::string::npos) {
                                        font_name = da_string.substr(font_start + 1, font_end - font_start - 1);
                                    }
                                }

                                // Extract font size (number before "Tf")
                                size_t tf_pos = da_string.find("Tf");
                                if (tf_pos != std::string::npos) {
                                    size_t size_start = da_string.rfind(' ', tf_pos - 1);
                                    if (size_start != std::string::npos) {
                                        size_start = da_string.rfind(' ', size_start - 1);
                                        if (size_start != std::string::npos) {
                                            std::string size_str =
                                                da_string.substr(size_start + 1, tf_pos - size_start - 1);
                                            font_size = std::stof(size_str);
                                        }
                                    }
                                }

                                // Extract color (three numbers before "rg")
                                size_t rg_pos = da_string.find("rg");
                                if (rg_pos != std::string::npos) {
                                    // Find the three color values before "rg"
                                    std::istringstream iss(da_string.substr(0, rg_pos));
                                    std::string token;
                                    std::vector<std::string> tokens;
                                    while (iss >> token) {
                                        tokens.push_back(token);
                                    }
                                    if (tokens.size() >= 3) {
                                        float rf = std::stof(tokens[tokens.size() - 3]);
                                        float gf = std::stof(tokens[tokens.size() - 2]);
                                        float bf = std::stof(tokens[tokens.size() - 1]);
                                        r = static_cast<int>(rf * 255);
                                        g = static_cast<int>(gf * 255);
                                        b = static_cast<int>(bf * 255);
                                    }
                                }
                            }
                        }

                        if (text && strlen(text) > 0) {
                            FontInfo loaded_font;
                            if (!font_name.empty())
                                loaded_font.family = font_name;
                            loaded_font.size = font_size;
                            loaded_font.color = {r, g, b};

                            // Convert rect top to baseline
                            AnnotationCoordinates coords;
                            float baseline_y = coords.rect_top_to_baseline_pdf(y1, font_size);

                            Annotation annotation(std::string(text), page_idx + 1, x, baseline_y, width, height,
                                                  loaded_font);
                            annotations.push_back(annotation);
                        }
                    }
                }

                annot = pdf_next_annot(ctx, annot);
            }

            pdf_drop_page(ctx, page);
        }
    }
    fz_catch(ctx)
    {
        logger::error("Error loading annotations: {}", fz_caught_message(ctx));
    }

    return annotations;
}


void Document::reload_annotations()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc) {
        logger::error("Failed to open document for annotation reload: {}", filename_.string());
        return;
    }

    std::vector<Annotation> loaded = load_annotations_from_pdf(ctx, doc);
    close_fitz(ctx, doc);

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
    annotations_ = std::move(loaded);

    logger::debug("Reloaded {} annotations from PDF", annotations_.size());
}


bool Document::save_annotations_to_pdf()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc) {
        logger::error("Failed to open document for annotation saving: {}", filename_.string());
        return false;
    }

    pdf_document* pdf = pdf_specifics(ctx, doc);
    if (!pdf) {
        logger::error("Not a PDF document: {}", filename_.string());
        close_fitz(ctx, doc);
        return false;
    }

    bool success = false;
    fz_try(ctx)
    {
        // Cache loaded pages to avoid reloading (which would see annotations we just added)
        std::map<int, pdf_page*> loaded_pages;

        // Add all our annotations
        for (const auto& annotation : annotations_) {
            int page_idx = annotation.page_num_ - 1;
            pdf_page* page = nullptr;

            auto it = loaded_pages.find(page_idx);
            if (it != loaded_pages.end()) {
                page = it->second;
            } else {
                page = pdf_load_page(ctx, pdf, page_idx);
                if (!page) {
                    logger::error("Failed to load page {} for annotation", annotation.page_num_);
                    continue;
                }
                loaded_pages[page_idx] = page;

                // Delete any existing FreeText annotations on this page
                // (pdf_load_page may have loaded cached annotations)
                pdf_annot* annot = pdf_first_annot(ctx, page);
                while (annot) {
                    pdf_annot* next = pdf_next_annot(ctx, annot);
                    if (pdf_annot_type(ctx, annot) == PDF_ANNOT_FREE_TEXT) {
                        pdf_delete_annot(ctx, page, annot);
                    }
                    annot = next;
                }
            }

            fz_rect page_bounds = fz_bound_page(ctx, (fz_page*)page);

            pdf_annot* annot = pdf_create_annot(ctx, page, PDF_ANNOT_FREE_TEXT);

            // Build annotation rect from baseline position
            float page_height = page_bounds.y1 - page_bounds.y0;
            AnnotationCoordinates coords;
            coords.set_context({page_height, page_bounds.x1 - page_bounds.x0, 0, 0});
            float font_size = annotation.font_info_.size;
            float rect_y0 = coords.baseline_to_rect_top_screen(annotation.y_, font_size);
            float rect_y1 = rect_y0 + annotation.height_;
            fz_rect rect = fz_make_rect(annotation.x_, rect_y0, annotation.x_ + annotation.width_, rect_y1);

            pdf_set_annot_rect(ctx, annot, rect);
            pdf_set_annot_contents(ctx, annot, annotation.text_.c_str());

            // Set default appearance (font and color)
            std::string mupdf_font_name = pdf_font_to_mupdf_font(annotation.font_info_.family);
            auto [cr, cg, cb] = annotation.font_info_.color;
            float color[3] = {cr / 255.0f, cg / 255.0f, cb / 255.0f};
            pdf_set_annot_default_appearance(ctx, annot, mupdf_font_name.c_str(), annotation.font_info_.size, 3, color);

            // Set quadding (alignment)
            pdf_set_annot_quadding(ctx, annot, 0);

            // Set border to invisible
            pdf_set_annot_border(ctx, annot, 0);

            // Update annotation to generate appearance stream
            pdf_update_annot(ctx, annot);

            pdf_drop_annot(ctx, annot);
        }

        // Drop all cached pages
        for (auto& [idx, page] : loaded_pages) {
            pdf_drop_page(ctx, page);
        }

        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 1;
        pdf_save_document(ctx, pdf, filename_.string().c_str(), &opts);

        success = true;
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception while saving annotations for {}: {}", filename_.string(),
                      fz_caught_message(ctx));
    }

    close_fitz(ctx, doc);
    return success;
}

// Generate txt filename from pdf filename
static std::filesystem::path get_bookmarks_txt_path(const std::filesystem::path& pdf_path)
{
    auto txt_path = pdf_path;
    txt_path.replace_extension(".txt");
    return txt_path;
}


bool Document::bookmarks_file_exists() const
{
    auto txt_path = get_bookmarks_txt_path(filename_);
    return std::filesystem::exists(txt_path);
}


bool Document::set_bookmarks_from_txt_file()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    auto txt_path = get_bookmarks_txt_path(filename_);

    if (!std::filesystem::exists(txt_path)) {
        std::string error_msg = std::format("Bookmark file does not exist: {}", txt_path.string());
        logger::error(error_msg);
        QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
        return false;
    }

    std::ifstream file(txt_path);
    if (!file.is_open()) {
        std::string error_msg = std::format("Failed to open bookmark file: {}", txt_path.string());
        logger::error(error_msg);
        QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
        return false;
    }

    std::vector<std::tuple<int, int, std::string>> parsed_bookmarks; // level, page, title
    std::vector<int> indent_stack;
    std::string line;
    int line_num = 0;
    int prev_page = 0;

    while (std::getline(file, line)) {
        ++line_num;

        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;

        // Calculate indent level
        std::string stripped = line;
        stripped.erase(0, stripped.find_first_not_of(" \t"));
        int indent_level = static_cast<int>(line.length() - stripped.length());

        // Parse page number and text
        std::istringstream iss(stripped);
        std::string page_str, title;
        if (!(iss >> page_str)) {
            std::string error_msg = std::format("Line {}: Invalid format - expected 'page_number text'", line_num);
            logger::error(error_msg);
            QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
            return false;
        }

        int page_num;
        try {
            page_num = std::stoi(page_str);
        } catch (const std::exception&) {
            std::string error_msg = std::format("Line {}: Invalid page number '{}'", line_num, page_str);
            logger::error(error_msg);
            QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
            return false;
        }

        if (page_num < 1) {
            std::string error_msg = std::format("Line {}: Page number must be positive, got {}", line_num, page_num);
            logger::error(error_msg);
            QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
            return false;
        }

        if (page_num < prev_page) {
            std::string error_msg =
                std::format("Line {}: Page numbers must be in order, got {} after {}", line_num, page_num, prev_page);
            logger::error(error_msg);
            QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
            return false;
        }

        if (page_num > page_count()) {
            std::string error_msg =
                std::format("Line {}: Page {} does not exist (PDF has {} pages)", line_num, page_num, page_count());
            logger::error(error_msg);
            QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
            return false;
        }

        prev_page = page_num;

        // Get remaining text as title
        std::string remaining;
        std::getline(iss, remaining);
        title = remaining;
        if (!title.empty() && title[0] == ' ')
            title = title.substr(1); // Remove leading space

        if (title.empty()) {
            std::string error_msg = std::format("Line {}: Missing bookmark title", line_num);
            logger::error(error_msg);
            QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
            return false;
        }

        // Determine hierarchy level based on indentation
        int level;
        if (indent_level == 0) {
            level = 1;
            indent_stack.clear();
            indent_stack.push_back(indent_level);
        } else {
            // Find appropriate level in stack
            while (!indent_stack.empty() && indent_level <= indent_stack.back())
                indent_stack.pop_back();

            indent_stack.push_back(indent_level);
            level = static_cast<int>(indent_stack.size());
        }

        parsed_bookmarks.emplace_back(level, page_num, title);
    }

    file.close();

    if (parsed_bookmarks.empty()) {
        std::string error_msg = std::format("No valid bookmarks found in {}", txt_path.string());
        logger::error(error_msg);
        QMessageBox::warning(nullptr, "Bookmark Import Error", QString::fromStdString(error_msg));
        return false;
    }

    // All parsing and validation passed, now update bookmarks
    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    undo_stack_.push_back(bookmarks_);
    bookmarks_.clear();

    std::vector<Bookmark*> bookmark_level_stack; // Track parent at each level

    for (const auto& [level, page_num, title] : parsed_bookmarks) {
        Bookmark bookmark(title, page_num);

        // Adjust stack size to current level
        if (level <= static_cast<int>(bookmark_level_stack.size())) {
            bookmark_level_stack.resize(level - 1);
        }

        if (level == 1) {
            // Top level bookmark
            bookmarks_.push_back(bookmark);
            bookmark_level_stack.clear();
            bookmark_level_stack.push_back(&bookmarks_.back());
        } else {
            // Child bookmark - add to parent at level-1
            Bookmark* parent = bookmark_level_stack.back();
            parent->add_child(bookmark);

            // Update the newly added child's parent handle
            auto& new_child = parent->children_.back();
            new_child.parent_handle_ = parent->handle_;

            bookmark_level_stack.push_back(&new_child);
        }
    }

    modified_ = true;
    emit bookmarks_loaded();
    logger::info("Successfully loaded {} bookmarks from {}", parsed_bookmarks.size(), txt_path.string());
    return true;
}

bool Document::save_bookmarks_to_txt_file()
{
    SAFE_METHOD;
    TRACE_FUNCTION;

    if (bookmarks_.size() == 0)
        return false;

    auto txt_path = filename_;
    txt_path.replace_extension(".txt");

    std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

    std::ofstream file(txt_path);
    if (!file.is_open()) {
        std::string error_msg = std::format("Failed to open bookmark file for writing: {}", txt_path.string());
        logger::error(error_msg);
        return false;
    }

    std::function<void(const Bookmark&, int)> write_bookmark = [&](const Bookmark& bm, int depth) {
        if (bm.page_num_.has_value()) {
            std::string indent(depth * 4, ' ');
            file << indent << bm.page_num_.value() << " " << bm.title_ << "\n";
        }
        for (const auto& child : bm.children_)
            write_bookmark(child, depth + 1);
    };

    for (const auto& bm : bookmarks_)
        write_bookmark(bm, 0);

    file.close();
    logger::info("Successfully saved bookmarks to {}", txt_path.string());
    return true;
}
