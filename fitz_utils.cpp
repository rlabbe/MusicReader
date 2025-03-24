#include "fitz_utils.h"
#include <excpt.h>


PixmapData render_page_seh(fz_context *ctx, fz_document *doc, int page_num, int dpi)
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
        result.data = static_cast<unsigned char *>(malloc(result.size));
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
