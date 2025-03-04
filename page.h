#pragma once

#include <QPixmap>
#include <QImage>
#include <QSize>

#include "border.h"

QPixmap resize_by_border(QPixmap img, Border border, int relief);

struct Page {
    QPixmap img;
    int page_num{ 1 };
    Border border;
    bool double_page{ false }; // use if constructed from 2 pages for viewing

    Page() {}
    explicit Page(int num) : page_num(num) {}

    Page(const QPixmap &image, int page_number, bool doubled)
        : img(image), page_num(page_number), double_page(doubled)
    {
        border = find_content_edges(img.toImage());
    }

    bool is_empty() const { return img.isNull(); }
    QSize shape() const { return img.isNull() ? QSize() : img.size(); }
    QSize size() const { return shape(); }
    int width() const { return shape().width(); }
    int height() const { return shape().height(); }

    QPixmap resize_by_border(int relief=0)
    {
        return ::resize_by_border(img, border, relief);
    }
};


inline QPixmap resize_by_border(QPixmap img, Border border, int relief = 0)
{
    if (img.isNull()) return img;

    int left = std::max(0, border.left - relief);
    int top = std::max(0, border.top - relief);
    int width = border.right - border.left + (2 * relief);
    int height = border.bottom - border.top + (2 * relief);

    // Crop the image based on the border
    return img.copy(left, top, width, height);
}


inline QRect border_to_qrect(const Border &border, int relief)
{
    return QRect(border.left - relief, 
                 border.top - relief,
                 border.right - border.left + (2 * relief),
                 border.bottom - border.top + (2 * relief));
}


