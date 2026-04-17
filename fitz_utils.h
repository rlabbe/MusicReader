#pragma once

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <utility>
#include <string>
#include <atomic>
#include <vector>
#include <QImage>
#include "bookmark.h"
#include <filesystem>


class pdf_document;

// cannot have constructor/destructor or unique_ptr, because being used inside __try
struct PixmapData {
    fz_context* ctx;
    fz_pixmap* data;
    int width;
    int height;
    int stride;
    int depth; // size / width
    int size;
    bool success;
};

extern std::pair<fz_context*, fz_document*> open_fitz(const std::filesystem::path& filename);
extern void close_fitz(fz_context* ctx, fz_document* doc);

// Render a page from a document to a QPixmap. quit_now may change value if the
// read thead is prematurely shut down (e.g. app close). Function tries to
// exit as soon as possible to make ui as fast as possible.
extern QImage::Format image_format(const PixmapData& data);
extern PixmapData
render_page_seh(fz_context* ctx, fz_document* doc, int page_num, int dpi, std::atomic<bool>& quit_now);


enum class BookmarkResult {
    Success,
    ContextCreationFailed,
    DocumentOpenFailed,
    NotPdfDocument,
    NoDocumentRoot,
    SaveFailed,
    MuPdfException
};

extern BookmarkResult add_bookmarks_to_pdf(const std::filesystem::path& pdf_filename,
                                           const std::vector<Bookmark>& bookmarks);


enum class TextResult {
    Success,
    ContextCreationFailed,
    DocumentOpenFailed,
    NotPdfDocument,
    PageNotFound,
    SaveFailed,
    MuPdfException
};

extern TextResult add_text_to_pdf(const std::filesystem::path& pdf_filename,
                                  const std::string& text,
                                  int page_num, // 1-based
                                  float x,
                                  float y, // in PDF points, (0,0) = bottom-left
                                  float font_size,
                                  const std::string& font_name,
                                  int r,
                                  int g,
                                  int b);

// Encode a single Unicode codepoint as UTF-8. Used for SMuFL PUA codepoints.
extern std::string codepoint_to_utf8(int codepoint);

// Measure a single glyph's advance width and total ink height in points,
// loading the font from disk on demand. Used to size music-symbol annotations
// before placement so the on-page rect matches what we draw.
struct GlyphMetrics {
    float width;
    float height;
    float ascent;
    float descent;
};
extern GlyphMetrics measure_glyph(const std::filesystem::path& font_file,
                                  const std::string& font_family,
                                  int codepoint,
                                  float font_size);

// Add a FreeText annotation that uses an embedded CID font (e.g. Bravura SMuFL).
// MuPDF's own appearance writer hardcodes Base-14 fonts and silently rewrites
// unknown DA font names to Helvetica (see pdf-appearance.c full_font_name()).
// We bypass that by building the appearance stream by hand as a Form XObject
// whose /Resources carry the embedded font, then injecting it via
// pdf_set_annot_appearance — mupdf marks the annot as resynthesised and won't
// regenerate over our stream.
extern TextResult add_freetext_with_custom_font(const std::filesystem::path& pdf_filename,
                                                const std::string& text,
                                                int page_num, // 1-based
                                                float x,
                                                float y, // PDF points, (0,0) = bottom-left, lower-left of rect
                                                float width,
                                                float height,
                                                float font_size,
                                                const std::string& font_family,
                                                const std::filesystem::path& font_file, // TTF/OTF
                                                int r,
                                                int g,
                                                int b);

// Utility function for PDFViewer to convert pixel coordinates to PDF points
extern std::pair<float, float> pixels_to_pdf_points(int pixel_x,
                                                    int pixel_y,
                                                    int page_width_pixels,
                                                    int page_height_pixels,
                                                    int page_width_points,
                                                    int page_height_points);

extern QImage qimage_from_pixmapdata(const PixmapData& data);
extern QImage render_page(fz_context* ctx, fz_document* doc, int page_num, int dpi, std::atomic<bool>& quit_now);
extern bool delete_all_freetext_annotations(fz_context* ctx, pdf_document* pdf);

extern bool delete_annotation_by_content_and_position(fz_context* ctx,
                                                      pdf_document* pdf,
                                                      int target_page,
                                                      const std::string& target_text,
                                                      float target_x,
                                                      float target_y,
                                                      float tolerance = 1.0f);
