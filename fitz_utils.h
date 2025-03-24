#pragma once

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <utility>
#include <string>

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
PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi);



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






