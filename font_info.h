#pragma once

#include <string>
#include <tuple>
#include <optional>

// Forward declarations for Qt types used in function signatures
class QString;
class QSize;
class QPoint;


struct FontInfo {
    std::string family = "Helvetica";  // PDF Base-14 font name
    float size = 10.0f;
    std::tuple<int, int, int> color = {0, 0, 0};  // RGB
};


// Maps PDF Base-14 font names to Qt system font names for rendering
QString pdf_font_to_qt_font(const std::string& pdf_font);

// Maps PDF Base-14 font names to MuPDF's internal short names for appearance stream generation
std::string pdf_font_to_mupdf_font(const std::string& pdf_font);

// Converts Qt font name and style back to PDF Base-14 font name
std::string qt_font_to_pdf_font(const QString& qt_font, bool bold, bool italic);

// Calculate text size in pixels for given font
QSize calculate_text_size(const QString& text, const FontInfo& font_info);

// Calculate adjusted position accounting for font metrics and margins
QPoint calculate_adjusted_position(const QPoint& click_pos, const FontInfo& font_info);

// Looks up a font file path from the Windows Registry given a font family name.
// Returns the full path to the .ttf file, or nullopt if not found.
std::optional<std::string> lookup_font_file(const std::string& font_family);
