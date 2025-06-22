#pragma once

#include <QString>
#include <QColor>
#include <QTextDocument>
#include <QTextEdit>
#include <QFontMetrics>


struct FontInfo {
    QString family = "Consolas";
    float size = 14.0f;
    QColor color = QColor(255, 0, 0);
};



/*
inline QSize calculate_text_size(const QString &text, const FontInfo &font_info)
{
    QTextDocument doc;
    doc.setPlainText(text);
    QFont font(font_info.family, static_cast<int>(font_info.size));
    doc.setDefaultFont(font);
    doc.adjustSize();
    QSize size = doc.size().toSize();
    size.setWidth(size.width() + 4);   // Small padding for visual comfort
    size.setHeight(size.height() + 2); // Small padding for visual comfort
    return size;
}*/

/*
inline QSize calculate_text_size(const QString &text, const FontInfo &font_info)
{
    QFont font(font_info.family, static_cast<int>(font_info.size));
    QFontMetrics fm(font);

    // Use actual font metrics instead of QTextDocument
    int width = fm.horizontalAdvance(text);
    int height = fm.height();

    // Add small padding for visual comfort
    width += 4;   // 2px padding on each side
    height += 4;  // 2px padding top and bottom

    return QSize(width, height);
}*/

inline QSize calculate_text_size(const QString &text, const FontInfo &font_info)
{
    QFont font(font_info.family, static_cast<int>(font_info.size));
    QFontMetrics fm(font);
    [[maybe_unused]] QRect r = fm.boundingRect(text);

    int width = fm.horizontalAdvance(text);
    int height = fm.height();

    // Add small padding
    width += 4;
    height += 4;

    return QSize(width, height);
}

inline QPoint calculate_adjusted_position(const QPoint &click_pos, const FontInfo &font_info)
{
    QTextDocument doc;
    QFont font(font_info.family, static_cast<int>(font_info.size));
    QFontMetrics fm(font);

    QPoint adjusted_pos = click_pos;
    adjusted_pos.setX(click_pos.x() - static_cast<int>(doc.documentMargin()) - 2);
    adjusted_pos.setY(click_pos.y() - fm.ascent() - static_cast<int>(doc.documentMargin()));
    return adjusted_pos;
}