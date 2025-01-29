#pragma once

#include <QPixmap>
#include <QImage>
#include <QSize>

#include "border.h"


struct Page {
    QPixmap img;
    int page_num{ 1 };
    Border border;
    bool double_page{ false }; // use if constructed from 2 pages for viewing

    Page() {}

    Page(const QPixmap &image, int page_number, bool doubled=false)
        : img(image), page_num(page_number), double_page(doubled)
    {
        border = find_content_edges(img.toImage());
    }

    bool is_empty() const { return img.isNull(); }

    QSize shape() const { return img.isNull() ? QSize() : img.size(); }
    QSize size() const { return shape(); }
    int width() const { return shape().width(); }
    int height() const { return shape().height(); }
};

