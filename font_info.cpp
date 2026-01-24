#include "font_info.h"
#include "logger.h"
#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <QString>
#include <QFont>
#include <QFontMetrics>
#include <QTextDocument>


QString pdf_font_to_qt_font(const std::string& pdf_font)
{
    if (pdf_font.starts_with("Courier"))
        return "Courier New";
    else if (pdf_font.starts_with("Helvetica"))
        return "Arial";
    else if (pdf_font.starts_with("Times"))
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

QSize calculate_text_size(const QString& text, const FontInfo& font_info)
{
    QString qt_font_name = pdf_font_to_qt_font(font_info.family);
    QFont font(qt_font_name);
    font.setPixelSize(static_cast<int>(font_info.size));
    QFontMetrics fm(font);

    int width = fm.horizontalAdvance(text);
    int height = fm.height();

    // Add small padding
    width += 4;
    height += 4;

    return QSize(width, height);
}

QPoint calculate_adjusted_position(const QPoint& click_pos, const FontInfo& font_info)
{
    QTextDocument doc;
    QString qt_font_name = pdf_font_to_qt_font(font_info.family);
    QFont font(qt_font_name, static_cast<int>(font_info.size));
    QFontMetrics fm(font);

    QPoint adjusted_pos = click_pos;
    adjusted_pos.setX(click_pos.x() - static_cast<int>(doc.documentMargin()) - 2);
    adjusted_pos.setY(click_pos.y() - fm.ascent() - static_cast<int>(doc.documentMargin()));
    return adjusted_pos;
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
                logger::info("Found font file: {} for family '{}'", full_path.string(), font_family);
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
