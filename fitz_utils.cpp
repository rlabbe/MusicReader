#include "fitz_utils.h"
#include <mutex>
#include <format>
#include <algorithm>
#include "utils.h"

#pragma warning(push, 1)
#include <mupdf/pdf.h>
#pragma warning(pop)

#include "logger.h"
#pragma warning(disable : 4611) // disable warning about _setjump not working with c++ destructors

// Global MuPDF locking setup
static std::mutex mupdf_mutexes[FZ_LOCK_MAX];


void lock_mutex(void* user, int lock)
{
    static_cast<std::mutex*>(user)[lock].lock();
}


void unlock_mutex(void* user, int lock)
{
    static_cast<std::mutex*>(user)[lock].unlock();
}


fz_locks_context get_locks_context()
{
    static fz_locks_context locks = {.user = mupdf_mutexes, .lock = lock_mutex, .unlock = unlock_mutex};
    return locks;
}


inline PixmapData render_page_seh(fz_context* ctx, fz_document* doc, int page_num, int dpi, std::atomic<bool>& quit_now)
{
    PixmapData result {
        .ctx = ctx, .data = nullptr, .width = 0, .height = 0, .stride = 0, .depth = 0, .size = 0, .success = false};

    if (quit_now || !ctx || !doc)
        return result;

    __try {
        fz_matrix transform = fz_scale(dpi / 72.0f, dpi / 72.0f);
        result.data = fz_new_pixmap_from_page_number(ctx, doc, page_num, transform, fz_device_rgb(ctx), 0);
        if (!result.data)
            return result;
        if (quit_now)
            return result;

        result.width = fz_pixmap_width(ctx, result.data);
        result.height = fz_pixmap_height(ctx, result.data);
        result.stride = fz_pixmap_components(ctx, result.data) * result.width;
        result.size = result.stride * result.height;
        result.depth = result.stride / result.width;
        result.success = true;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // logger::error("Access violation while rendering page " + std::to_string(page_num));
    }

    return result;
}


inline void close_fitz(fz_context* ctx, fz_document* doc)
{
    fz_flush_warnings(ctx);

    if (!ctx)
        return;
    if (doc)
        fz_drop_document(ctx, doc);
    doc = nullptr;
    if (ctx)
        fz_drop_context(ctx);
}


inline std::pair<fz_context*, fz_document*> open_fitz(const std::filesystem::path& filename)
{
    fz_locks_context locks = get_locks_context();
    fz_context* ctx = fz_new_context(nullptr, &locks, FZ_STORE_DEFAULT);
    if (!ctx)
        return {nullptr, nullptr};
    fz_try(ctx)
    {
        fz_set_aa_level(ctx, 0);
        fz_register_document_handlers(ctx);
    }
    fz_catch(ctx)
    {
        close_fitz(ctx, nullptr);
        return {nullptr, nullptr};
    }
    fz_document* doc = nullptr;
    fz_try(ctx)
    {
#ifdef _WIN32
        std::string utf8_filename = wide_to_utf8(filename.wstring());
        doc = fz_open_document(ctx, utf8_filename.c_str());
#else
        doc = fz_open_document(ctx, filename.c_str());
#endif
    }
    fz_catch(ctx)
    {
        close_fitz(ctx, doc);
        return {nullptr, nullptr};
    }
    return {ctx, doc};
}


QImage::Format image_format(const PixmapData& data)
{
    if (!data.success || !data.data)
        return QImage::Format_Invalid;

    // Handle RGBA directly
    if (data.depth == 4)
        return QImage::Format_RGBA8888;

    unsigned char* samples = fz_pixmap_samples(data.ctx, data.data);

    // Already grayscale
    if (data.depth == 1) {
        for (int y = 0; y < data.height; y++) {
            for (int x = 0; x < data.width; x++) {
                int offset = y * data.stride + x;
                unsigned char val = samples[offset];
                if (val != 0 && val != 255)
                    return QImage::Format_Grayscale8;
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
                if (r != g || g != b)
                    return QImage::Format_RGB888;

                // Track if it's monochrome
                if (r != 0 && r != 255)
                    is_mono = false;
            }
        }

        // If we get here, it's grayscale - check if mono
        return is_mono ? QImage::Format_Mono : QImage::Format_Grayscale8;
    }

    // Default case
    return QImage::Format_RGB888;
}


BookmarkResult add_bookmarks_to_pdf(const std::filesystem::path& pdf_filename, const std::vector<Bookmark>& bookmarks)
{
    fz_context* ctx = nullptr;
    fz_document* fz_doc = nullptr;
    pdf_document* pdf = nullptr;

    ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return BookmarkResult::ContextCreationFailed;

    fz_register_document_handlers(ctx);

    fz_try(ctx)
    {
        std::string utf8_filename = wide_to_utf8(pdf_filename.wstring());

        fz_doc = fz_open_document(ctx, utf8_filename.c_str());
        if (!fz_doc) {
            fz_drop_context(ctx);
            return BookmarkResult::DocumentOpenFailed;
        }

        pdf = pdf_specifics(ctx, fz_doc);
        if (!pdf) {
            fz_drop_document(ctx, fz_doc);
            fz_drop_context(ctx);
            return BookmarkResult::NotPdfDocument;
        }

        // Get document root
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        if (!root) {
            fz_drop_document(ctx, fz_doc);
            fz_drop_context(ctx);
            return BookmarkResult::NoDocumentRoot;
        }

        // Remove existing outline if it exists
        pdf_obj* existing_outlines = pdf_dict_get(ctx, root, PDF_NAME(Outlines));
        if (existing_outlines)
            pdf_dict_del(ctx, root, PDF_NAME(Outlines));

        // If not empty vector, create new outline structure
        if (!bookmarks.empty()) {
            pdf_obj* outlines_dict = pdf_new_dict(ctx, pdf, 3);
            pdf_obj* outlines = pdf_add_object(ctx, pdf, outlines_dict);
            pdf_drop_obj(ctx, outlines_dict);

            pdf_dict_put(ctx, root, PDF_NAME(Outlines), outlines);
            pdf_dict_put(ctx, outlines, PDF_NAME(Type), PDF_NAME(Outlines));

            // Recursive function to create bookmark items
            std::function<pdf_obj*(const std::vector<Bookmark>&, pdf_obj*)> create_bookmarks =
                [&](const std::vector<Bookmark>& bmarks, pdf_obj* parent) -> pdf_obj* {
                pdf_obj* first = nullptr;
                pdf_obj* last = nullptr;
                int count = 0;

                for (const auto& bookmark : bmarks) {
                    // Create bookmark item as indirect object
                    pdf_obj* item_dict = pdf_new_dict(ctx, pdf, 6);
                    pdf_obj* item = pdf_add_object(ctx, pdf, item_dict);
                    pdf_drop_obj(ctx, item_dict);

                    // Set title
                    pdf_dict_put_text_string(ctx, item, PDF_NAME(Title), bookmark.title_.c_str());

                    // Set destination if page number exists
                    if (bookmark.page_num_.has_value()) {
                        pdf_obj* dest_array = pdf_new_array(ctx, pdf, 2);
                        pdf_obj* page_ref = pdf_lookup_page_obj(ctx, pdf, bookmark.page_num_.value() - 1);
                        if (page_ref) {
                            pdf_array_push(ctx, dest_array, page_ref);
                            pdf_array_push(ctx, dest_array, PDF_NAME(Fit));
                            pdf_dict_put(ctx, item, PDF_NAME(Dest), dest_array);
                        }
                    }

                    // Set parent
                    pdf_dict_put(ctx, item, PDF_NAME(Parent), parent);

                    // Handle children
                    if (!bookmark.children_.empty()) {
                        pdf_obj* child_first = create_bookmarks(bookmark.children_, item);
                        if (child_first) {
                            pdf_dict_put(ctx, item, PDF_NAME(First), child_first);
                            // Find last child
                            pdf_obj* child_last = child_first;
                            while (pdf_dict_get(ctx, child_last, PDF_NAME(Next))) {
                                child_last = pdf_dict_get(ctx, child_last, PDF_NAME(Next));
                            }
                            pdf_dict_put(ctx, item, PDF_NAME(Last), child_last);
                            pdf_dict_put_int(ctx, item, PDF_NAME(Count), static_cast<int>(bookmark.children_.size()));
                        }
                    }

                    // Link siblings
                    if (!first) {
                        first = item;
                    } else {
                        pdf_dict_put(ctx, last, PDF_NAME(Next), item);
                        pdf_dict_put(ctx, item, PDF_NAME(Prev), last);
                    }
                    last = item;
                    count++;
                }

                // Update parent's count
                if (first) {
                    pdf_dict_put_int(ctx, parent, PDF_NAME(Count), count);
                }

                return first;
            };

            // Create all bookmarks
            pdf_obj* first_bookmark = create_bookmarks(bookmarks, outlines);
            if (first_bookmark) {
                pdf_dict_put(ctx, outlines, PDF_NAME(First), first_bookmark);

                // Find last top-level bookmark
                pdf_obj* last_bookmark = first_bookmark;
                while (pdf_dict_get(ctx, last_bookmark, PDF_NAME(Next))) {
                    last_bookmark = pdf_dict_get(ctx, last_bookmark, PDF_NAME(Next));
                }
                pdf_dict_put(ctx, outlines, PDF_NAME(Last), last_bookmark);
            }
        }

        // Save document incrementally
        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 1;
        pdf_save_document(ctx, pdf, utf8_filename.c_str(), &opts);
    }
    fz_catch(ctx)
    {
        logger::error("MuPDF exception in add_bookmarks_to_pdf: {}", fz_caught_message(ctx));
        if (fz_doc)
            fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return BookmarkResult::MuPdfException;
    }

    if (fz_doc)
        fz_drop_document(ctx, fz_doc);
    fz_drop_context(ctx);
    return BookmarkResult::Success;
}

TextResult add_text_to_pdf(const std::filesystem::path& pdf_filename,
                           const std::string& text,
                           int page_num,
                           float x,
                           float y,
                           float font_size,
                           const std::string& font_name,
                           int r,
                           int g,
                           int b)
{
    fz_context* ctx = nullptr;
    fz_document* fz_doc = nullptr;
    pdf_document* pdf = nullptr;

    ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    fz_set_aa_level(ctx, 0);
    if (!ctx) {
        logger::error("Failed to create MuPDF context");
        return TextResult::ContextCreationFailed;
    }

    fz_register_document_handlers(ctx);

    // Open document
    std::string utf8_filename = wide_to_utf8(pdf_filename.wstring());
    fz_try(ctx)
    {
        fz_doc = fz_open_document(ctx, utf8_filename.c_str());
    }
    fz_catch(ctx)
    {
        logger::error("Failed to open PDF document '{}': {}", utf8_filename, fz_caught_message(ctx));
        fz_drop_context(ctx);
        return TextResult::DocumentOpenFailed;
    }

    if (!fz_doc) {
        logger::error("Document handle is null after opening '{}'", utf8_filename);
        fz_drop_context(ctx);
        return TextResult::DocumentOpenFailed;
    }

    // Get PDF specifics
    fz_try(ctx)
    {
        pdf = pdf_specifics(ctx, fz_doc);
    }
    fz_catch(ctx)
    {
        logger::error("Failed to get PDF specifics for '{}': {}", utf8_filename, fz_caught_message(ctx));
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::MuPdfException;
    }

    if (!pdf) {
        logger::error("Document '{}' is not a valid PDF", utf8_filename);
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::NotPdfDocument;
    }

    // Check page count and validate page number
    int page_count = 0;
    fz_try(ctx)
    {
        page_count = fz_count_pages(ctx, fz_doc);
    }
    fz_catch(ctx)
    {
        logger::error("Failed to count pages in '{}': {}", utf8_filename, fz_caught_message(ctx));
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::MuPdfException;
    }

    if (page_num < 1 || page_num > page_count) {
        logger::error("Page {} not found in '{}' (document has {} pages)", page_num, utf8_filename, page_count);
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::PageNotFound;
    }

    // Get page object
    pdf_obj* page_obj = nullptr;
    fz_try(ctx)
    {
        page_obj = pdf_lookup_page_obj(ctx, pdf, page_num - 1);
    }
    fz_catch(ctx)
    {
        logger::error("Failed to lookup page {} in '{}': {}", page_num, utf8_filename, fz_caught_message(ctx));
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::MuPdfException;
    }

    if (!page_obj) {
        logger::error("Page object {} is null in '{}'", page_num, utf8_filename);
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::PageNotFound;
    }

    // Create and configure annotation
    fz_try(ctx)
    {
        // Create annotation dictionary
        pdf_obj* annot_dict = pdf_new_dict(ctx, pdf, 10);
        pdf_obj* annot = pdf_add_object(ctx, pdf, annot_dict);
        pdf_drop_obj(ctx, annot_dict);

        // Set required annotation properties
        pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

        // Set rectangle
        float width = font_size * text.length() * 0.6f;
        float height = font_size * 1.2f;
        pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
        pdf_array_push_real(ctx, rect, x);
        pdf_array_push_real(ctx, rect, y);
        pdf_array_push_real(ctx, rect, x + width);
        pdf_array_push_real(ctx, rect, y + height);
        pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);

        // Set content
        pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), text.c_str());

        // Set page reference
        pdf_dict_put(ctx, annot, PDF_NAME(P), page_obj);

        // Set flags (print flag)
        pdf_dict_put_int(ctx, annot, PDF_NAME(F), 4);

        // Remove the C (color) property to avoid background color
        // The text color will be set in the DA string instead

        // Create default appearance string with text color only
        char da_buf[256];
        snprintf(da_buf, sizeof(da_buf), "/%s %.1f Tf %.3f %.3f %.3f rg", font_name.c_str(), font_size, r / 255.0f,
                 g / 255.0f, b / 255.0f);
        pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), da_buf);

        // Set quadding (text alignment) - 0 = left, 1 = center, 2 = right
        pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 0);

        // Add border style to make background transparent
        pdf_obj* bs_dict = pdf_new_dict(ctx, pdf, 2);
        pdf_dict_put_int(ctx, bs_dict, PDF_NAME(W), 0); // Border width 0
        pdf_dict_put(ctx, annot, PDF_NAME(BS), bs_dict);

        // Add annotation to page annotations array
        pdf_obj* annots = pdf_dict_get(ctx, page_obj, PDF_NAME(Annots));
        if (!annots) {
            annots = pdf_new_array(ctx, pdf, 1);
            pdf_dict_put(ctx, page_obj, PDF_NAME(Annots), annots);
        }
        pdf_array_push(ctx, annots, annot);

        // Save document incrementally
        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 1;
        pdf_save_document(ctx, pdf, utf8_filename.c_str(), &opts);
    }
    fz_catch(ctx)
    {
        logger::error("Failed to add text annotation '{}' to page {} of '{}': {}", text, page_num, utf8_filename,
                      fz_caught_message(ctx));
        fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::MuPdfException;
    }

    logger::info("Successfully added text '{}' to page {} of '{}'", text, page_num, utf8_filename);
    fz_drop_document(ctx, fz_doc);
    fz_drop_context(ctx);
    return TextResult::Success;
}


std::string codepoint_to_utf8(int cp)
{
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}


GlyphMetrics measure_glyph(const std::filesystem::path& font_file,
                           const std::string& font_family,
                           int codepoint,
                           float font_size)
{
    GlyphMetrics m {0.0f, 0.0f, 0.0f, 0.0f};

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return m;

    fz_font* font = nullptr;
    fz_try(ctx)
    {
        font = fz_new_font_from_file(ctx, font_family.c_str(), font_file.string().c_str(), 0, 1);
        int gid = fz_encode_character(ctx, font, codepoint);
        float advance = fz_advance_glyph(ctx, font, gid, 0);
        // Per-glyph ink box, not the font-wide bbox. SMuFL fonts have a huge
        // font_bbox covering the largest possible glyph (8va lines, brackets);
        // a single flat or sharp occupies a tiny fraction of that.
        fz_rect gb = fz_bound_glyph(ctx, font, gid, fz_identity);
        m.width = advance * font_size;
        m.ascent = gb.y1 * font_size;
        m.descent = -gb.y0 * font_size;
        m.height = m.ascent + m.descent;
    }
    fz_catch(ctx)
    {
        logger::error("measure_glyph failed for U+{:04X} in '{}': {}",
                      codepoint, font_family, fz_caught_message(ctx));
    }

    if (font)
        fz_drop_font(ctx, font);
    fz_drop_context(ctx);
    return m;
}


TextResult add_freetext_with_custom_font(const std::filesystem::path& pdf_filename,
                                         const std::string& text,
                                         int page_num,
                                         float x,
                                         float y,
                                         float width,
                                         float height,
                                         float font_size,
                                         const std::string& font_family,
                                         const std::filesystem::path& font_file,
                                         int r,
                                         int g,
                                         int b)
{
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return TextResult::ContextCreationFailed;

    fz_register_document_handlers(ctx);

    fz_document* fz_doc = nullptr;
    pdf_document* pdf = nullptr;
    fz_font* fz_font_obj = nullptr;
    pdf_page* page = nullptr;
    pdf_annot* annot = nullptr;
    pdf_obj* res = nullptr;
    fz_buffer* buf = nullptr;

    std::string utf8_filename = wide_to_utf8(pdf_filename.wstring());

    // Internal name for the font inside the Form XObject's resource dict.
    // Scope is only the appearance stream, so a fixed short name is fine.
    constexpr const char* kFormFontName = "F1";

    fz_try(ctx)
    {
        fz_doc = fz_open_document(ctx, utf8_filename.c_str());
        pdf = pdf_specifics(ctx, fz_doc);
        if (!pdf)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Not a PDF document");

        int page_count = fz_count_pages(ctx, fz_doc);
        if (page_num < 1 || page_num > page_count)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Page not found");

        page = pdf_load_page(ctx, pdf, page_num - 1);

        fz_font_obj = fz_new_font_from_file(ctx, font_family.c_str(), font_file.string().c_str(), 0, 1);

        // Form XObject's own resource dict — carries the embedded CID font so
        // viewers that don't consult page /Resources for annotation APs still resolve it.
        res = pdf_new_dict(ctx, pdf, 1);
        pdf_obj* res_font = pdf_dict_put_dict(ctx, res, PDF_NAME(Font), 1);
        pdf_dict_puts_drop(ctx, res_font, kFormFontName, pdf_add_cid_font(ctx, pdf, fz_font_obj));

        // SMuFL fonts have a huge font-wide bbox. For tight rects (a single
        // sharp shouldn't be sized like an 8va bracket), walk the actual text
        // and union the per-glyph ink bounds.
        float ascent = 0.0f, descent = 0.0f;
        {
            const char* p = text.c_str();
            const char* end = p + text.size();
            bool any = false;
            while (p < end) {
                int rune = 0;
                int n = fz_chartorune(&rune, p);
                int gid = fz_encode_character(ctx, fz_font_obj, rune);
                fz_rect gb = fz_bound_glyph(ctx, fz_font_obj, gid, fz_identity);
                float a = gb.y1 * font_size;
                float d = -gb.y0 * font_size;
                if (!any) { ascent = a; descent = d; any = true; }
                else { ascent = std::max(ascent, a); descent = std::max(descent, d); }
                p += n;
            }
        }
        float ink_height = ascent + descent;

        // Build the appearance stream. Origin (0,0) is bottom-left of bbox;
        // baseline sits one descent above the bottom so descenders fit.
        buf = fz_new_buffer(ctx, 256);
        fz_append_string(ctx, buf, "q\n");
        fz_append_printf(ctx, buf, "%.3f %.3f %.3f rg\n", r / 255.0f, g / 255.0f, b / 255.0f);
        fz_append_string(ctx, buf, "BT\n");
        fz_append_printf(ctx, buf, "/%s %.2f Tf\n", kFormFontName, font_size);
        fz_append_printf(ctx, buf, "0 %.2f Td\n", descent);
        fz_append_string(ctx, buf, "<");
        const char* p = text.c_str();
        const char* end = p + text.size();
        while (p < end)
        {
            int rune = 0;
            int n = fz_chartorune(&rune, p);
            int gid = fz_encode_character(ctx, fz_font_obj, rune);
            // CID fonts use 2-byte glyph indices in show strings.
            fz_append_printf(ctx, buf, "%04x", gid);
            p += n;
        }
        fz_append_string(ctx, buf, "> Tj\n");
        fz_append_string(ctx, buf, "ET\n");
        fz_append_string(ctx, buf, "Q\n");

        // Expand the on-page rect height to fit the font's ink box if the
        // caller's height is too small. Width is left to the caller.
        float final_height = std::max(height, ink_height);

        annot = pdf_create_annot(ctx, page, PDF_ANNOT_FREE_TEXT);
        fz_rect rect = fz_make_rect(x, y, x + width, y + final_height);
        pdf_set_annot_rect(ctx, annot, rect);
        pdf_set_annot_contents(ctx, annot, text.c_str());
        pdf_set_annot_border(ctx, annot, 0);

        // Write /DA so any future re-edit by Acrobat/Foxit has a font hint.
        // mupdf's own writer would map this back to Helvetica, but we override
        // its appearance below before that ever runs.
        char da[96];
        snprintf(da, sizeof(da), "/%s %.2f Tf %.3f %.3f %.3f rg",
                 kFormFontName, font_size, r / 255.0f, g / 255.0f, b / 255.0f);
        pdf_dict_put_string(ctx, pdf_annot_obj(ctx, annot), PDF_NAME(DA), da, strlen(da));

        // Inject our hand-built form. mupdf calls pdf_set_annot_resynthesised
        // internally so its appearance writer leaves us alone.
        // Bbox must match the rect dimensions so the form maps 1:1 (no scaling).
        fz_rect bbox = fz_make_rect(0, 0, width, final_height);
        pdf_set_annot_appearance(ctx, annot, "N", nullptr, fz_identity, bbox, res, buf);

        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 1;
        pdf_save_document(ctx, pdf, utf8_filename.c_str(), &opts);
    }
    fz_catch(ctx)
    {
        logger::error("Failed to add custom-font annotation to '{}': {}", utf8_filename, fz_caught_message(ctx));
        if (annot)
            pdf_drop_annot(ctx, annot);
        if (page)
            pdf_drop_page(ctx, page);
        if (res)
            pdf_drop_obj(ctx, res);
        if (buf)
            fz_drop_buffer(ctx, buf);
        if (fz_font_obj)
            fz_drop_font(ctx, fz_font_obj);
        if (fz_doc)
            fz_drop_document(ctx, fz_doc);
        fz_drop_context(ctx);
        return TextResult::MuPdfException;
    }

    if (annot)
        pdf_drop_annot(ctx, annot);
    if (page)
        pdf_drop_page(ctx, page);
    if (res)
        pdf_drop_obj(ctx, res);
    if (buf)
        fz_drop_buffer(ctx, buf);
    if (fz_font_obj)
        fz_drop_font(ctx, fz_font_obj);
    if (fz_doc)
        fz_drop_document(ctx, fz_doc);
    fz_drop_context(ctx);
    return TextResult::Success;
}


// Utility function for PDFViewer to convert pixel coordinates to PDF points
std::pair<float, float> pixels_to_pdf_points(int pixel_x,
                                             int pixel_y,
                                             int page_width_pixels,
                                             int page_height_pixels,
                                             int page_width_points,
                                             int page_height_points)
{
    // Convert from top-left pixel coordinates to bottom-left PDF points
    float pdf_x = (float(pixel_x) / page_width_pixels) * page_width_points;
    float pdf_y = page_height_points - (float(pixel_y) / page_height_pixels) * page_height_points;

    return {pdf_x, pdf_y};
}

QImage qimage_from_pixmapdata(const PixmapData& data)
{
    unsigned char* samples = fz_pixmap_samples(data.ctx, data.data);

    // Determine the optimal format based on actual image content
    QImage::Format format = image_format(data);

    // Source is always RGB from mupdf, but we convert to grayscale if content is grayscale
    QImage source_img(samples, data.width, data.height, data.stride, QImage::Format_RGB888);

    if (format == QImage::Format_Grayscale8 || format == QImage::Format_Mono) {
        return source_img.convertToFormat(QImage::Format_Grayscale8);
    }

    return source_img.copy();
}


QImage render_page(fz_context* ctx, fz_document* doc, int page_num, int dpi, std::atomic<bool>& quit_now)
{
    PixmapData data = render_page_seh(ctx, doc, page_num, dpi, quit_now);
    if (!data.success)
        return QImage();

    QImage img = qimage_from_pixmapdata(data);
    fz_drop_pixmap(ctx, data.data);
    return img;
}

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


bool delete_annotation_by_content_and_position(fz_context* ctx,
                                               pdf_document* pdf,
                                               int target_page,
                                               const std::string& target_text,
                                               float target_x,
                                               float target_y,
                                               float tolerance)
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
