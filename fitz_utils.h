#pragma once

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <utility>
#include <string>
#include <atomic>
#include <excpt.h>

struct PixmapData {
    unsigned char *data;
    int width;
    int height;
    int stride;
    int size;
    bool success;
};

void close_fitz(fz_context *ctx, fz_document *doc);
std::pair<fz_context *, fz_document *> open_fitz(const std::string &filename);

// Render a page from a document to a QPixmap. quit_now may change value if the 
// read thead is prematurely shut down (e.g. app close). Function tries to 
// exit as soon as possible to make ui as fast as possible.
PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi, std::atomic<bool>& quit_now);



inline void close_fitz(fz_context *ctx, fz_document *doc)
{
    fz_flush_warnings(ctx);

    if (!ctx) return;
    if (doc)
        fz_drop_document(ctx, doc);
    doc = nullptr;
    if (ctx) fz_drop_context(ctx);
}


inline std::pair<fz_context *, fz_document *> open_fitz(const std::string &filename)
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


inline PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi, std::atomic<bool> &quit_now)
{

    PixmapData result = { nullptr, 0, 0, 0, 0, false };
    fz_pixmap *temp_pixmap = nullptr;

    if (quit_now) return result;

    __try {
        fz_matrix transform = fz_scale(dpi / 72.0f, dpi / 72.0f);
        temp_pixmap = fz_new_pixmap_from_page_number(ctx, doc, page_num, transform, fz_device_rgb(ctx), 0);
        if (!temp_pixmap) return result;
        if (quit_now) return result;

        result.width = fz_pixmap_width(ctx, temp_pixmap);
        result.height = fz_pixmap_height(ctx, temp_pixmap);
        result.stride = fz_pixmap_components(ctx, temp_pixmap) * result.width;
        result.size = result.stride * result.height;

        // Allocate memory for a deep copy
        if (quit_now) {
            fz_drop_pixmap(ctx, temp_pixmap);
            return result;
        }

        result.data = static_cast<unsigned char *>(malloc(result.size));
        if (result.data) {
            memcpy(result.data, fz_pixmap_samples(ctx, temp_pixmap), result.size);
            result.success = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        //logger::log_error("Access violation while rendering page " + std::to_string(page_num));
    }

    fz_drop_pixmap(ctx, temp_pixmap);
    return result;
}




