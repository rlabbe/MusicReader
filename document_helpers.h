#pragma once
#pragma warning(disable : 4611) // disable warning about _setjump not working with c++ destructors

// Standalone annotation deletion functions for Document class
// Based on MuPDF's pdf-annot-edit.c patterns
bool delete_all_freetext_annotations(fz_context* ctx, pdf_document* pdf)
{
    if (!ctx || !pdf)
        return false;

    bool any_deleted = false;

    fz_try(ctx)
    {
        int page_count = pdf_count_pages(ctx, pdf);

        for (int page_idx = 0; page_idx < page_count; ++page_idx) {
            pdf_page* page = pdf_load_page(ctx, pdf, page_idx);
            if (!page)
                continue;

            // Get all annotations for this page
            pdf_annot* annot = pdf_first_annot(ctx, page);

            while (annot) {
                pdf_annot* next_annot = pdf_next_annot(ctx, annot);

                // Check if this is a FreeText annotation
                if (pdf_annot_type(ctx, annot) == PDF_ANNOT_FREE_TEXT) {
                    pdf_delete_annot(ctx, page, annot);
                    any_deleted = true;
                }

                annot = next_annot;
            }

            pdf_drop_page(ctx, page);
        }
    }
    fz_catch(ctx)
    {
        logger::error("Error deleting annotations: {}", fz_caught_message(ctx));
        return false;
    }

    return any_deleted;
}


bool delete_annotation_by_content_and_position(fz_context* ctx, pdf_document* pdf, int target_page,
                                               const std::string& target_text, float target_x, float target_y,
                                               float tolerance = 1.0f)
{
    if (!ctx || !pdf || target_page < 1)
        return false;

    bool deleted = false;
    pdf_page* page = nullptr;

    fz_try(ctx)
    {
        page = pdf_load_page(ctx, pdf, target_page - 1);
    }
    fz_catch(ctx)
    {
        logger::error("Couldn't get page {}: {}", target_page, fz_caught_message(ctx));
        return false;
    }

    if (!page) {
        logger::error("Couldn't get page {}", target_page);
        return false;
    }


    fz_try(ctx)
    {
        pdf_annot* annot = pdf_first_annot(ctx, page);

        while (annot) {
            pdf_annot* next_annot = pdf_next_annot(ctx, annot);

            if (pdf_annot_type(ctx, annot) == PDF_ANNOT_FREE_TEXT) {
                // Check content match
                const char* contents = pdf_annot_contents(ctx, annot);
                if (contents && target_text == contents) {
                    // Check position match
                    fz_rect rect = pdf_annot_rect(ctx, annot);
                    if (fabs(rect.x0 - target_x) <= tolerance && fabs(rect.y1 - target_y) <= tolerance) {
                        pdf_delete_annot(ctx, page, annot);
                        deleted = true;
                        break; // Assuming we only want to delete the first match
                    }
                }
            }

            annot = next_annot;
        }

        pdf_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        logger::error("Error deleting specific annotation: {}", fz_caught_message(ctx));
        return false;
    }

    return deleted;
}


/*
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
                mat = cv::Mat(converted.height(), converted.width(), CV_8UC3, converted.bits(),
converted.bytesPerLine());
        }
        return mat.clone();
}*/

inline std::string to_string(QImage::Format format)
{
    switch (format) {
        case QImage::Format_Invalid: return "Format_Invalid";
        case QImage::Format_Mono: return "Format_Mono";
        case QImage::Format_MonoLSB: return "Format_MonoLSB";
        case QImage::Format_Indexed8: return "Format_Indexed8";
        case QImage::Format_RGB32: return "Format_RGB32";
        case QImage::Format_ARGB32: return "Format_ARGB32";
        case QImage::Format_ARGB32_Premultiplied: return "Format_ARGB32_Premultiplied";
        case QImage::Format_RGB16: return "Format_RGB16";
        case QImage::Format_ARGB8565_Premultiplied: return "Format_ARGB8565_Premultiplied";
        case QImage::Format_RGB666: return "Format_RGB666";
        case QImage::Format_ARGB6666_Premultiplied: return "Format_ARGB6666_Premultiplied";
        case QImage::Format_RGB555: return "Format_RGB555";
        case QImage::Format_ARGB8555_Premultiplied: return "Format_ARGB8555_Premultiplied";
        case QImage::Format_RGB888: return "Format_RGB888";
        case QImage::Format_RGB444: return "Format_RGB444";
        case QImage::Format_ARGB4444_Premultiplied: return "Format_ARGB4444_Premultiplied";
        case QImage::Format_RGBX8888: return "Format_RGBX8888";
        case QImage::Format_RGBA8888: return "Format_RGBA8888";
        case QImage::Format_RGBA8888_Premultiplied: return "Format_RGBA8888_Premultiplied";
        case QImage::Format_BGR30: return "Format_BGR30";
        case QImage::Format_A2BGR30_Premultiplied: return "Format_A2BGR30_Premultiplied";
        case QImage::Format_RGB30: return "Format_RGB30";
        case QImage::Format_A2RGB30_Premultiplied: return "Format_A2RGB30_Premultiplied";
        case QImage::Format_Alpha8: return "Format_Alpha8";
        case QImage::Format_Grayscale8: return "Format_Grayscale8";
        case QImage::Format_RGBX64: return "Format_RGBX64";
        case QImage::Format_RGBA64: return "Format_RGBA64";
        case QImage::Format_RGBA64_Premultiplied: return "Format_RGBA64_Premultiplied";
        case QImage::Format_Grayscale16: return "Format_Grayscale16";
        case QImage::Format_BGR888: return "Format_BGR888";
        case QImage::Format_RGBX16FPx4: return "Format_RGBX16FPx4";
        case QImage::Format_RGBA16FPx4: return "Format_RGBA16FPx4";
        case QImage::Format_RGBA16FPx4_Premultiplied: return "Format_RGBA16FPx4_Premultiplied";
        case QImage::Format_RGBX32FPx4: return "Format_RGBX32FPx4";
        case QImage::Format_RGBA32FPx4: return "Format_RGBA32FPx4";
        case QImage::Format_RGBA32FPx4_Premultiplied: return "Format_RGBA32FPx4_Premultiplied";
        case QImage::Format_CMYK8888: return "Format_CMYK8888";
        default: return "Unknown Format (" + std::to_string(static_cast<int>(format)) + ")";
    }
}


inline int get_max_screen_height()
{
    static int max_screen_height = []() {
        int max_height = 0;
        const auto screens = QGuiApplication::screens();
        for (const auto* screen : screens) {
            max_height = std::max(max_height, screen->size().height());
        }
        return max_height;
    }();
    return max_screen_height;
}


inline QImage qimage_from_pixmapdata(const PixmapData& data)
{
    unsigned char* samples = fz_pixmap_samples(data.ctx, data.data);

    // Create initial QImage with the source data
    QImage source_img(samples, data.width, data.height, data.stride, QImage::Format_RGB888);
    return source_img.copy();
}


inline QImage render_page(fz_context* ctx, fz_document* doc, int page_num, int dpi, std::atomic<bool>& quit_now)
{
    PixmapData data = render_page_seh(ctx, doc, page_num, dpi, quit_now);
    if (!data.success)
        return QImage();

    QImage img = qimage_from_pixmapdata(data);
    fz_drop_pixmap(ctx, data.data);
    return img;
}
