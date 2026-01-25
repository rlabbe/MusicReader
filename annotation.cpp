#include "annotation.h"
#include <atomic>
#include <QFontMetrics>
#include <QFont>
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#pragma warning(pop)
#include "logger.h"


Annotation::Annotation(const std::string& text,
                       int page_num,
                       float x,
                       float y,
                       float width,
                       float height,
                       const FontInfo& font_info)
    : handle_(generate_uuid())
    , page_num_(page_num)
    , x_(x)
    , y_(y)
    , width_(width)
    , height_(height)
    , text_(text)
    , font_info_(font_info)
{
}


int Annotation::generate_uuid()
{
    static std::atomic<int> id = 0;
    ++id;
    return id;
}


std::vector<Annotation> load_annotations_from_pdf(fz_context* ctx, fz_document* doc)
{
    std::vector<Annotation> annotations;

    if (!ctx || !doc)
        return annotations;

    pdf_document* pdf = pdf_specifics(ctx, doc);
    if (!pdf)
        return annotations;

    fz_try(ctx)
    {
        int page_count = fz_count_pages(ctx, doc);

        for (int page_idx = 0; page_idx < page_count; ++page_idx) {
            pdf_obj* page_obj = pdf_lookup_page_obj(ctx, pdf, page_idx);
            if (!page_obj)
                continue;

            pdf_obj* annots = pdf_dict_get(ctx, page_obj, PDF_NAME(Annots));
            if (!annots)
                continue;

            int annot_count = pdf_array_len(ctx, annots);
            for (int i = 0; i < annot_count; ++i) {
                pdf_obj* annot = pdf_array_get(ctx, annots, i);
                if (!annot)
                    continue;

                pdf_obj* subtype = pdf_dict_get(ctx, annot, PDF_NAME(Subtype));
                if (pdf_name_eq(ctx, subtype, PDF_NAME(FreeText))) {
                    // Extract annotation properties
                    pdf_obj* rect = pdf_dict_get(ctx, annot, PDF_NAME(Rect));
                    pdf_obj* contents = pdf_dict_get(ctx, annot, PDF_NAME(Contents));

                    if (rect && contents) {
                        float x = pdf_array_get_real(ctx, rect, 0);
                        float y = pdf_array_get_real(ctx, rect, 3);
                        float width = pdf_array_get_real(ctx, rect, 2) - pdf_array_get_real(ctx, rect, 0);
                        float height = pdf_array_get_real(ctx, rect, 3) - pdf_array_get_real(ctx, rect, 1);

                        const char* text = pdf_to_text_string(ctx, contents);

                        // Extract font, size, and color from default appearance
                        std::string font_name;
                        float font_size = 12.0f;
                        int r = 0, g = 0, b = 0;

                        // Parse DA (Default Appearance) string
                        pdf_obj* da = pdf_dict_get(ctx, annot, PDF_NAME(DA));
                        if (da) {
                            const char* da_str = pdf_to_text_string(ctx, da);
                            if (da_str) {
                                // Parse DA string format: "/FontName FontSize Tf r g b rg"
                                std::string da_string(da_str);

                                // Extract font name (starts with /)
                                size_t font_start = da_string.find('/');
                                if (font_start != std::string::npos) {
                                    size_t font_end = da_string.find(' ', font_start);
                                    if (font_end != std::string::npos) {
                                        font_name = da_string.substr(font_start + 1, font_end - font_start - 1);
                                    }
                                }

                                // Extract font size (number before "Tf")
                                size_t tf_pos = da_string.find("Tf");
                                if (tf_pos != std::string::npos) {
                                    size_t size_start = da_string.rfind(' ', tf_pos - 1);
                                    if (size_start != std::string::npos) {
                                        size_start = da_string.rfind(' ', size_start - 1);
                                        if (size_start != std::string::npos) {
                                            std::string size_str =
                                                da_string.substr(size_start + 1, tf_pos - size_start - 1);
                                            font_size = std::stof(size_str);
                                        }
                                    }
                                }

                                // Extract color (three numbers before "rg")
                                size_t rg_pos = da_string.find("rg");
                                if (rg_pos != std::string::npos) {
                                    // Find the three color values before "rg"
                                    std::istringstream iss(da_string.substr(0, rg_pos));
                                    std::string token;
                                    std::vector<std::string> tokens;
                                    while (iss >> token) {
                                        tokens.push_back(token);
                                    }
                                    if (tokens.size() >= 3) {
                                        float rf = std::stof(tokens[tokens.size() - 3]);
                                        float gf = std::stof(tokens[tokens.size() - 2]);
                                        float bf = std::stof(tokens[tokens.size() - 1]);
                                        r = static_cast<int>(rf * 255);
                                        g = static_cast<int>(gf * 255);
                                        b = static_cast<int>(bf * 255);
                                    }
                                }
                            }
                        }

                        if (text && strlen(text) > 0) {
                            FontInfo loaded_font;
                            if (!font_name.empty())
                                loaded_font.family = font_name;
                            loaded_font.size = font_size;
                            loaded_font.color = {r, g, b};

                            Annotation annotation(std::string(text), page_idx + 1, x, y, width, height, loaded_font);

                            auto [cr, cg, cb] = annotation.font_info_.color;
                            logger::debug(
                                "LOAD: page={}, x={}, y={}, w={}, h={}, text='{}', font='{}' {}pt, color=({},{},{})",
                                annotation.page_num_, annotation.x_, annotation.y_, annotation.width_,
                                annotation.height_, annotation.text_, annotation.font_info_.family,
                                annotation.font_info_.size, cr, cg, cb);

                            // After getting the rect values, log them:
                            logger::debug("RAW RECT: [{:.2f}, {:.2f}, {:.2f}, {:.2f}]",
                                          pdf_array_get_real(ctx, rect, 0), pdf_array_get_real(ctx, rect, 1),
                                          pdf_array_get_real(ctx, rect, 2), pdf_array_get_real(ctx, rect, 3));
                            annotations.push_back(annotation);
                        }
                    }
                }
            }
        }
    }
    fz_catch(ctx)
    {
        logger::error("Error loading annotations: {}", fz_caught_message(ctx));
    }

    return annotations;
}
