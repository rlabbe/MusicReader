#pragma once

#include <string>
#include <tuple>
#include <optional>

// Forward declaration for Qt type used in function signatures
class QString;


struct FontInfo {
    std::string family = "Helvetica"; // PDF Base-14 font name
    float size = 10.0f;
    std::tuple<int, int, int> color = {0, 0, 0}; // RGB
};


// Maps PDF Base-14 font names to Qt system font names for rendering
QString pdf_font_to_qt_font(const std::string& pdf_font);

// Maps PDF Base-14 font names to MuPDF's internal short names for appearance stream generation
std::string pdf_font_to_mupdf_font(const std::string& pdf_font);

// Converts Qt font name and style back to PDF Base-14 font name
std::string qt_font_to_pdf_font(const QString& qt_font, bool bold, bool italic);

// Looks up a font file path from the Windows Registry given a font family name.
// Returns the full path to the .ttf file, or nullopt if not found.
std::optional<std::string> lookup_font_file(const std::string& font_family);

// Normalizes a PDF font name to the MuPDF Base-14 font name.
// Handles short names like "Helv" -> "Helvetica", "Cour" -> "Courier", etc.
std::string normalize_to_base14_font(const std::string& pdf_font_name);

// Measures text width using MuPDF's Base-14 font metrics.
// Returns width in PDF points. This matches what MuPDF actually renders.
// pdf_font_name can be Base-14 name or short name (will be normalized internally)
float mupdf_measure_text_width(const std::string& pdf_font_name, float font_size, const std::string& text);

// Measures text height using MuPDF's Base-14 font metrics.
// Returns the font's ascent + |descent| in PDF points.
float mupdf_measure_text_height(const std::string& pdf_font_name, float font_size);

// Returns font ascent (distance from baseline to top) in PDF points.
float mupdf_font_ascent(const std::string& pdf_font_name, float font_size);

// Returns font descent (distance from baseline to bottom, as positive value) in PDF points.
float mupdf_font_descent(const std::string& pdf_font_name, float font_size);
