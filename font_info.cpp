#include "font_info.h"
#include "logger.h"
#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <QString>

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)


QString pdf_font_to_qt_font(const std::string& pdf_font)
{
    if (pdf_font.starts_with("Courier") || pdf_font.starts_with("Cour"))
        return "Courier New";
    else if (pdf_font.starts_with("Helvetica") || pdf_font.starts_with("Helv"))
        return "Arial";
    else if (pdf_font.starts_with("Times") || pdf_font.starts_with("TiRo"))
        return "Times New Roman";
    else if (pdf_font == "Symbol")
        return "Symbol";
    else if (pdf_font == "ZapfDingbats")
        return "Wingdings";
    else
        return QString::fromStdString(pdf_font); // Unknown, use as-is
}


std::string pdf_font_to_mupdf_font(const std::string& pdf_font)
{
    if (pdf_font.starts_with("Courier"))
        return "Cour";
    else if (pdf_font.starts_with("Helvetica"))
        return "Helv";
    else if (pdf_font.starts_with("Times"))
        return "TiRo";
    else if (pdf_font == "Symbol")
        return "Symb";
    else if (pdf_font == "ZapfDingbats")
        return "ZaDb";
    else
        return "Helv"; // Default fallback
}


std::string qt_font_to_pdf_font(const QString& qt_font, bool bold, bool italic)
{
    if (qt_font == "Courier New") {
        if (bold && italic)
            return "Courier-BoldOblique";
        if (bold)
            return "Courier-Bold";
        if (italic)
            return "Courier-Oblique";
        return "Courier";
    } else if (qt_font == "Arial") {
        if (bold && italic)
            return "Helvetica-BoldOblique";
        if (bold)
            return "Helvetica-Bold";
        if (italic)
            return "Helvetica-Oblique";
        return "Helvetica";
    } else if (qt_font == "Times New Roman") {
        if (bold && italic)
            return "Times-BoldItalic";
        if (bold)
            return "Times-Bold";
        if (italic)
            return "Times-Italic";
        return "Times-Roman";
    } else if (qt_font == "Symbol")
        return "Symbol";
    else if (qt_font == "Wingdings")
        return "ZapfDingbats";
    else
        return qt_font.toStdString();
}


std::optional<std::string> lookup_font_file(const std::string& font_family)
{
    TRACE_FUNCTION_MSG("Looking up font: {}", font_family);

    // Get Windows Fonts directory
    wchar_t fonts_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, 0, fonts_path))) {
        logger::error("Failed to get Windows Fonts directory");
        return std::nullopt;
    }

    std::filesystem::path fonts_dir(fonts_path);

    // Open registry key for fonts
    HKEY hkey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
                                KEY_READ, &hkey);

    if (result != ERROR_SUCCESS) {
        logger::error("Failed to open fonts registry key");
        return std::nullopt;
    }

    std::optional<std::string> found_file;
    DWORD index = 0;
    wchar_t value_name[512];
    BYTE value_data[512];

    QString search_family = QString::fromStdString(font_family);

    // Iterate through all registry values
    while (true) {
        DWORD name_size = sizeof(value_name) / sizeof(wchar_t);
        DWORD data_size = sizeof(value_data);
        DWORD type;

        result = RegEnumValueW(hkey, index, value_name, &name_size, nullptr, &type, value_data, &data_size);

        if (result == ERROR_NO_MORE_ITEMS)
            break;

        if (result != ERROR_SUCCESS) {
            logger::warning("Failed to enumerate registry value at index {}", index);
            index++;
            continue;
        }

        // value_name contains something like "Georgia (TrueType)"
        // value_data contains the filename like "georgia.ttf"
        QString reg_font_name = QString::fromWCharArray(value_name);
        QString reg_font_file = QString::fromWCharArray(reinterpret_cast<wchar_t*>(value_data));

        // Check if this registry entry matches our font family
        // The registry name format is typically "FontName (TrueType)"
        if (reg_font_name.contains(search_family, Qt::CaseInsensitive)) {
            // Build full path
            std::filesystem::path full_path = fonts_dir / reg_font_file.toStdWString();

            if (std::filesystem::exists(full_path)) {
                found_file = full_path.string();
                break;
            }
        }

        index++;
    }

    RegCloseKey(hkey);

    if (!found_file)
        logger::warning("Font '{}' not found in Windows Registry", font_family);

    return found_file;
}

std::string normalize_to_base14_font(const std::string& pdf_font_name)
{
    // Handle short MuPDF font names and map to proper Base-14 names
    if (pdf_font_name == "Helv" || pdf_font_name.starts_with("Helvetica"))
        return "Helvetica";
    if (pdf_font_name == "Helv-Bold" || pdf_font_name == "Helvetica-Bold")
        return "Helvetica-Bold";
    if (pdf_font_name == "Helv-Oblique" || pdf_font_name == "Helvetica-Oblique")
        return "Helvetica-Oblique";
    if (pdf_font_name == "Helv-BoldOblique" || pdf_font_name == "Helvetica-BoldOblique")
        return "Helvetica-BoldOblique";

    if (pdf_font_name == "Cour" || pdf_font_name.starts_with("Courier"))
        return "Courier";
    if (pdf_font_name == "Cour-Bold" || pdf_font_name == "Courier-Bold")
        return "Courier-Bold";
    if (pdf_font_name == "Cour-Oblique" || pdf_font_name == "Courier-Oblique")
        return "Courier-Oblique";
    if (pdf_font_name == "Cour-BoldOblique" || pdf_font_name == "Courier-BoldOblique")
        return "Courier-BoldOblique";

    if (pdf_font_name == "TiRo" || pdf_font_name.starts_with("Times"))
        return "Times-Roman";
    if (pdf_font_name == "TiRo-Bold" || pdf_font_name == "Times-Bold")
        return "Times-Bold";
    if (pdf_font_name == "TiRo-Italic" || pdf_font_name == "Times-Italic")
        return "Times-Italic";
    if (pdf_font_name == "TiRo-BoldItalic" || pdf_font_name == "Times-BoldItalic")
        return "Times-BoldItalic";

    if (pdf_font_name == "Symb" || pdf_font_name == "Symbol")
        return "Symbol";
    if (pdf_font_name == "ZaDb" || pdf_font_name == "ZapfDingbats")
        return "ZapfDingbats";

    // Default to Helvetica for unknown fonts
    return "Helvetica";
}

float text_width(const std::string& pdf_font_name, float font_size, const std::string& text)
{
    std::string base14_name = normalize_to_base14_font(pdf_font_name);
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        logger::error("mupdf_measure_text_width: failed to create context");
        return 0.0f;
    }

    float width = 0.0f;
    fz_font* font = nullptr;

    fz_try(ctx)
    {
        font = fz_new_base14_font(ctx, base14_name.c_str());
        if (!font) {
            logger::error("mupdf_measure_text_width: failed to load font '{}'", base14_name);
        }
        else {
            // Measure each character - fz_advance_glyph returns width in em units (1.0 = font_size)
            const char* s = text.c_str();
            while (*s) {
                int c = static_cast<unsigned char>(*s++);
                int glyph = fz_encode_character(ctx, font, c);
                width += fz_advance_glyph(ctx, font, glyph, 0);
            }
            // Scale by font size to get points
            width *= font_size;
        }
    }
    fz_always(ctx)
    {
        if (font)
            fz_drop_font(ctx, font);
    }
    fz_catch(ctx)
    {
        logger::error("mupdf_measure_text_width: exception measuring text");
    }

    fz_drop_context(ctx);
    return width;
}

float text_height(const std::string& pdf_font_name, float font_size)
{
    std::string base14_name = normalize_to_base14_font(pdf_font_name);
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        logger::error("mupdf_measure_text_height: failed to create context");
        return font_size; // Fallback
    }

    float height = font_size; // Fallback
    fz_font* font = nullptr;

    fz_try(ctx)
    {
        font = fz_new_base14_font(ctx, base14_name.c_str());
        if (!font) {
            logger::error("mupdf_measure_text_height: failed to load font '{}'", base14_name);
        }
        else {
            // ascender and descender are in em units
            // Note: descender is typically negative
            float ascender = fz_font_ascender(ctx, font);
            float descender = fz_font_descender(ctx, font);
            height = (ascender - descender) * font_size;
        }
    }
    fz_always(ctx)
    {
        if (font)
            fz_drop_font(ctx, font);
    }
    fz_catch(ctx)
    {
        logger::error("mupdf_measure_text_height: exception getting font metrics");
    }

    fz_drop_context(ctx);
    return height;
}

float font_ascent(const std::string& pdf_font_name, float font_size)
{
    std::string base14_name = normalize_to_base14_font(pdf_font_name);
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        logger::error("mupdf_font_ascent: failed to create context");
        return font_size * 0.8f; // Reasonable fallback
    }

    float ascent = font_size * 0.8f;
    fz_font* font = nullptr;

    fz_try(ctx)
    {
        font = fz_new_base14_font(ctx, base14_name.c_str());
        if (!font) {
            logger::error("mupdf_font_ascent: failed to load font '{}'", base14_name);
        }
        else {
            ascent = fz_font_ascender(ctx, font) * font_size;
        }
    }
    fz_always(ctx)
    {
        if (font)
            fz_drop_font(ctx, font);
    }
    fz_catch(ctx)
    {
        logger::error("mupdf_font_ascent: exception getting font metrics");
    }

    fz_drop_context(ctx);
    return ascent;
}

float font_descent(const std::string& pdf_font_name, float font_size)
{
    std::string base14_name = normalize_to_base14_font(pdf_font_name);
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        logger::error("mupdf_font_descent: failed to create context");
        return font_size * 0.2f; // Reasonable fallback
    }

    float descent = font_size * 0.2f;
    fz_font* font = nullptr;

    fz_try(ctx)
    {
        font = fz_new_base14_font(ctx, base14_name.c_str());
        if (!font) {
            logger::error("mupdf_font_descent: failed to load font '{}'", base14_name);
        }
        else {
            // descender is negative in MuPDF, return as positive
            descent = -fz_font_descender(ctx, font) * font_size;
        }
    }
    fz_always(ctx)
    {
        if (font)
            fz_drop_font(ctx, font);
    }
    fz_catch(ctx)
    {
        logger::error("mupdf_font_descent: exception getting font metrics");
    }

    fz_drop_context(ctx);
    return descent;
}
