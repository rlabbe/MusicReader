#include "document.h"
#include "logger.h"

#include <format>
#include <stdexcept>
#include <thread>
#include <future>
#include <QImage>


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


bool Document::save(const std::filesystem::path &filename)
{
    /*auto [ctx, doc] = open_fitz(filename_.string());
    if (!ctx || !doc)
    {
        throw std::runtime_error("Failed to save document to " + filename.string());
    }
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx)
    {
        throw std::runtime_error("Failed to create MuPDF context");
    }

    fz_document *doc = nullptr;
    fz_try(ctx)
    {
        // TODO makes no sense, open just to close, add code here for bookmarks and annotations
        doc = fz_open_document(ctx, filename_.string().c_str());
        fz_save_document(ctx, doc, filename.string().c_str(), nullptr);
    }
    fz_catch(ctx)
    {
        throw std::runtime_error("Failed to save document to " + filename.string());
    }
    close_fitz(ctx, doc);

    */
    return true;
}



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
    //TODO
    return nullptr;
}

bool Document::reparent_bookmark(const std::string &handle, const std::string &parent_handle, bool internal_call)
{
    //TODO
    return false;
}

bool Document::indent_bookmark(const std::string &handle)
{
    //TODO
    return false;
}

bool Document::unindent_bookmark(const std::string &handle)
{
    //TODO
    return false;
}

void Document::rename_bookmark(const std::string &handle, const std::string &title)
{
    //TODO
}

void Document::remove_bookmark(const std::string handle)
{
    //TODO
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


