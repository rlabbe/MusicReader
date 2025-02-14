#include "document.h"
#include "logger.h"

#include <format>
#include <stdexcept>
#include <thread>
#include <future>
#include <QImage>
#include "qpdf_document.h"

namespace {

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



QPixmap render_page(fz_context *ctx, fz_document *doc, int page_num, int dpi)
{
    fz_pixmap *temp_pixmap = nullptr;
    QPixmap pixmap;

    fz_try(ctx)
    {
        fz_matrix transform = fz_scale(dpi / 72.0f, dpi / 72.0f);
        temp_pixmap = fz_new_pixmap_from_page_number(ctx, doc, page_num, transform, fz_device_rgb(ctx), 0);

        int width = fz_pixmap_width(ctx, temp_pixmap);
        int height = fz_pixmap_height(ctx, temp_pixmap);
        int stride = fz_pixmap_components(ctx, temp_pixmap) * width;
        const uchar *data = fz_pixmap_samples(ctx, temp_pixmap);

        QImage img(data, width, height, stride, QImage::Format_RGB888);
        pixmap = QPixmap::fromImage(img);
    }
    fz_catch(ctx)
    {
        logger::log_error("Failed to render page " + std::to_string(page_num));
    }
    if (temp_pixmap) fz_drop_pixmap(ctx, temp_pixmap);

    return pixmap;
}




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



Document::Document(std::filesystem::path filename, int dpi)
    : filename_(std::move(filename)), dpi_(dpi)
{
    load_document();
}


Page Document::get_page(int page_num) const
{
    if (page_num < 1 || page_num > page_count()) {
        logger::log_error(std::format("Invalid page number: {} for {}",
                                      page_num, filename_.string()));
        return Page();
    }
    return pages_[page_num - 1];
}


void Document::load_document()
{
    int total_pages = 0;

    // Open document once to get page count 
    {
        auto [ctx, doc] = open_fitz(filename_.string());
        if (!ctx || !doc) {
            logger::log_error("Failed to open document: " + filename_.string());
            return;
        }

        total_pages = fz_count_pages(ctx, doc);
        close_fitz(ctx, doc);
    }

    if (total_pages == 0) {
        logger::log_error("Document has no pages: " + filename_.string());
        return;
    }

    pages_.resize(total_pages);

    // Launch threads for each page
    std::vector<std::future<QPixmap>> futures;
    for (int i = 0; i < total_pages; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i] {
            auto [thread_ctx, thread_doc] = open_fitz(filename_.string());
            if (!thread_ctx || !thread_doc) {
                logger::log_error("Failed to open document in thread for page " + std::to_string(i));
                return QPixmap();
            }

            QPixmap pixmap = render_page(thread_ctx, thread_doc, i, dpi_);
            close_fitz(thread_ctx, thread_doc);
            return pixmap;
        }));
    }

    // while these run we can get the bookmarks
    {
        auto [ctx, doc] = open_fitz(filename_.string());
        if (ctx && doc) {
            total_pages = fz_count_pages(ctx, doc);

            fz_outline *outline = fz_load_outline(ctx, doc);
            if (outline)
                bookmarks_ = convert_outline_to_bookmarks(outline);

            close_fitz(ctx, doc);
        }
    }

    // Collect results
    for (int i = 0; i < total_pages; ++i)
        pages_[i] = Page(futures[i].get(), i + 1);
}

/*
bool Document::save(const std::filesystem::path &output_filename, bool block)
{
    // just housekeeping, remove any futures that are done
    clear_completed_features();

    std::lock_guard<std::mutex> lock(save_mutex_);

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

Bookmark *Document::find_bookmark(const std::string &handle)
{
    for (auto &bookmark : bookmarks_) {
        if (bookmark.handle_ == handle) return &bookmark;
        auto child = bookmark.find(handle);
        if (child.has_value()) return &child.value();
    }
    return nullptr;
}



bool Document::reparent_bookmark(const std::string &handle, const std::string &parent_handle, bool internal_call)
{
    std::lock_guard<std::mutex> lock(save_mutex_);
    if (bookmarks_.empty()) return false;

    if (!internal_call) {
        undo_stack_.push_back(bookmarks_);
    }

    Bookmark *bookmark = find_bookmark(handle);
    if (!bookmark) return false;

    Bookmark temp = *bookmark;  // Copy the bookmark data before removing it

    // Remove from current parent if it had one
    if (bookmark->parent_handle_.has_value()) {
        Bookmark *old_parent = find_bookmark(bookmark->parent_handle_.value());
        if (old_parent) old_parent->remove_child(handle);
    } else {
        auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                               [&](const Bookmark &b) { return b.handle_ == handle; });
        if (it != bookmarks_.end()) bookmarks_.erase(it);
    }

    // Assign new parent or move to top level
    if (!parent_handle.empty()) {
        Bookmark *new_parent = find_bookmark(parent_handle);
        if (!new_parent) return false;
        new_parent->add_child(temp);
        temp.parent_handle_ = parent_handle;
        std::sort(new_parent->children_.begin(), new_parent->children_.end(), bookmark_sort);
    } else {
        bookmarks_.push_back(temp);
        temp.parent_handle_.reset();
        std::sort(bookmarks_.begin(), bookmarks_.end(), bookmark_sort);
    }

    save();
    return true;
}




bool Document::indent_bookmark(const std::string &handle)
{
    std::lock_guard<std::mutex> lock(save_mutex_);
    if (bookmarks_.empty()) return false;

    undo_stack_.push_back(bookmarks_);

    Bookmark *bookmark = find_bookmark(handle);
    if (!bookmark) return false;

    // If the bookmark is already top-level, find its previous sibling
    if (!bookmark->parent_handle_.has_value()) {
        auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                               [&](const Bookmark &b) { return b.handle_ == handle; });
        if (it == bookmarks_.begin()) return false;  // Cannot indent first item (no previous sibling)

        auto new_parent = std::prev(it);  // Move under previous sibling
        bookmarks_.erase(it);              // Remove from top-level list before reparenting
        return reparent_bookmark(handle, new_parent->handle_, true);
    } else {
        // Find the current parent and locate the previous sibling within that parent
        Bookmark *parent = find_bookmark(bookmark->parent_handle_.value());
        if (!parent) return false;  // Parent not found (shouldn't happen)

        auto it = std::find_if(parent->children_.begin(), parent->children_.end(),
                               [&](const Bookmark &b) { return b.handle_ == handle; });
        if (it == parent->children_.begin()) return false;  // Cannot indent first child (no previous sibling)

        auto new_parent = std::prev(it);   // Move under previous sibling
        parent->children_.erase(it);       // Remove from old parent before reparenting
        return reparent_bookmark(handle, new_parent->handle_, true);
    }
}


bool Document::unindent_bookmark(const std::string &handle)
{
    //TODO
    return false;
}

void Document::rename_bookmark(const std::string &handle, const std::string &title)
{
    std::lock_guard<std::mutex> lock(save_mutex_);
    if (bookmarks_.empty()) return;

    Bookmark *bookmark = find_bookmark(handle);
    if (bookmark && bookmark->title_ != title) {
        undo_stack_.push_back(bookmarks_);
        bookmark->title_ = title;
        save();
    }
}


void Document::remove_bookmark(const std::string &handle)
{
    std::lock_guard<std::mutex> lock(save_mutex_);
    if (bookmarks_.empty()) return;

    for (auto it = bookmarks_.begin(); it != bookmarks_.end(); ++it) {
        if (it->handle_ == handle) {
            undo_stack_.push_back(bookmarks_);
            bookmarks_.erase(it);
            save();
            return;
        }
        if (it->remove_child(handle)) {
            undo_stack_.push_back(bookmarks_);
            save();
            return;
        }
    }
}

Bookmark Document::add_bookmark(const std::string &title, int page_num, const std::string &parent_handle)
{
    undo_stack_.push_back(bookmarks_);  // Save for undo

    // Treat page_num == 0 as no page number (folder)
    std::optional<int> page_num_opt = (page_num == 0) ? std::nullopt : std::optional<int>(page_num);

    Bookmark new_bookmark(title, page_num_opt.has_value() ? page_num_opt.value() : 0);
    if (!parent_handle.empty()) {
        new_bookmark.parent_handle_ = parent_handle;
    }

    if (parent_handle.empty()) {
        bookmarks_.emplace_back(new_bookmark);
        std::sort(bookmarks_.begin(), bookmarks_.end(), bookmark_sort);
    } else {
        Bookmark *parent = find_bookmark(parent_handle);
        if (parent) {
            parent->add_child(new_bookmark);
            std::sort(parent->children_.begin(), parent->children_.end(), bookmark_sort);
        }
    }

    save();
    return new_bookmark;
}


bool Document::save(const std::filesystem::path &filename, bool block )
{
    filename;
    block;
    add_bookmarks_to_pdf(filename_.string(), bookmarks_);
    return true;
}



void save_annotations(fz_context *, fz_document *)
{
    //TODO
}




void Document::clear_completed_features()
{
    std::lock_guard<std::mutex> lock(save_mutex_);

    // Remove completed futures
    save_futures_.erase(std::remove_if(save_futures_.begin(), save_futures_.end(),
                                       [](std::future<void> &f) {
        return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }),
        save_futures_.end());
}



