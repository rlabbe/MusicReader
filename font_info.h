#pragma once

#include <QString>
#include <QColor>
#include <QTextDocument>
#include <QFontMetrics>


struct FontInfo {
    QString family = "Helvetica";
    float size = 14.0f;
    QColor color = QColor(255, 0, 0);
}; 




inline QSize calculate_text_size(const QString &text, const FontInfo &font_info)
{
    QTextDocument doc;
    doc.setPlainText(text);
    QFont font(font_info.family, static_cast<int>(font_info.size));
    doc.setDefaultFont(font);
    doc.adjustSize();
    QSize size = doc.size().toSize();
    size.setWidth(qMax(100, size.width()));
    size.setHeight(qMax(30, size.height()));
    return size;
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