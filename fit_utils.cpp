#include "fitz_utils.h"

#include "logger.h"
#include <format>

inline PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi, std::atomic<bool> &quit_now)
{
    PixmapData result{ .ctx = ctx, .data = nullptr, .width = 0, .height = 0,
                 .stride = 0, .depth = 0, .size = 0, .success = false };

    if (quit_now ||!ctx || !doc) return result;

    __try {
        fz_matrix transform = fz_scale(dpi / 72.0f, dpi / 72.0f);
        result.data = fz_new_pixmap_from_page_number(ctx, doc, page_num, transform, fz_device_rgb(ctx), 0);
        if (!result.data) return result;
        if (quit_now) return result;

        result.width = fz_pixmap_width(ctx, result.data);
        result.height = fz_pixmap_height(ctx, result.data);
        result.stride = fz_pixmap_components(ctx, result.data) * result.width;
        result.size = result.stride * result.height;
        result.depth = result.stride / result.width;
        result.success = true;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        //logger::error("Access violation while rendering page " + std::to_string(page_num));
    }

    return result;
}

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


QImage::Format image_format(const PixmapData &data)
{
    if (!data.success || !data.data) return QImage::Format_Invalid;

    // Handle RGBA directly
    if (data.depth == 4) return QImage::Format_RGBA8888;

    unsigned char *samples = fz_pixmap_samples(data.ctx, data.data);

    // Already grayscale
    if (data.depth == 1) {
        for (int y = 0; y < data.height; y++) {
            for (int x = 0; x < data.width; x++) {
                int offset = y * data.stride + x;
                unsigned char val = samples[offset];
                if (val != 0 && val != 255) return QImage::Format_Grayscale8;
            }
        }
        return QImage::Format_Mono;
    }

    // RGB format
    if (data.depth == 3) {
        bool is_mono = true;

        for (int y = 0; y < data.height; y++) {
            for (int x = 0; x < data.width; x++) {
                int offset = y * data.stride + x * 3;
                unsigned char r = samples[offset];
                unsigned char g = samples[offset + 1];
                unsigned char b = samples[offset + 2];

                // Not grayscale - return RGB immediately
                if (r != g || g != b) return QImage::Format_RGB888;

                // Track if it's monochrome
                if (r != 0 && r != 255) is_mono = false;
            }
        }

        // If we get here, it's grayscale - check if mono
        return is_mono ? QImage::Format_Mono : QImage::Format_Grayscale8;
    }

    // Default case
    return QImage::Format_RGB888;
}