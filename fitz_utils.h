#pragma once

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <utility>
#include <string>
#include <atomic>
#include <excpt.h>
#include <QImage>


// cannot have constructor/destructor or unique_ptr, because being used inside __try
struct PixmapData {
    fz_context *ctx;
    fz_pixmap *data;
    int width;
    int height;
    int stride;
    int depth;   // size / width
    int size;
    bool success;
};

extern std::pair<fz_context *, fz_document *> open_fitz(const std::string &filename);
extern void close_fitz(fz_context *ctx, fz_document *doc);

// Render a page from a document to a QPixmap. quit_now may change value if the 
// read thead is prematurely shut down (e.g. app close). Function tries to 
// exit as soon as possible to make ui as fast as possible.
extern QImage::Format image_format(const PixmapData &data);
extern PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi, std::atomic<bool> &quit_now);
