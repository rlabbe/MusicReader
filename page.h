#pragma once

#include <QPixmap>
#include <QImage>
#include <QSize>
#include "border.h"
#include <iostream>

QPixmap resize_by_border(QPixmap img, Border border, int relief);
QImage resize_by_border(QImage img, Border border, int relief);

struct Page {
    QImage img;
    int page_num{ 1 };
    Border border;
    bool double_page{ false }; // use if constructed from 2 pages for viewing

    Page() {}
    explicit Page(int num) : page_num(num) {}

    Page(const QImage &image, int page_number, bool doubled)
        : img(image), page_num(page_number), double_page(doubled)
    {
        border = find_content_edges(img);
    }

    bool is_empty() const { return img.isNull(); }
    QSize shape() const { return img.isNull() ? QSize() : img.size(); }
    QSize size() const { return shape(); }
    int width() const { return shape().width(); }
    int height() const { return shape().height(); }

    QPixmap as_pixmap() const
    {
        return as_pixmap(img);
    }

    static QPixmap as_pixmap(const QImage &image)
    {
        if (image.isNull()) return QPixmap();
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(image.devicePixelRatio());
        return pixmap;
    }


    QImage resize_by_border(int relief=0)
    {
        return ::resize_by_border(img, border, relief);
    }
};


struct PixmapPage : public Page {
    QPixmap pixmap;
    PixmapPage() {}
    PixmapPage(const Page &page)
        : Page(page), pixmap(Page::as_pixmap(page.img)){}
    

    PixmapPage(const QImage &image, int page_number, bool doubled)
        : Page(image, page_number, doubled), pixmap(Page::as_pixmap(image)) {}

    PixmapPage(const QPixmap &pixmap, int page_number, bool doubled)
        : Page(pixmap.toImage(), page_number, doubled), pixmap(pixmap){}
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

inline QImage resize_by_border(QImage img, Border border, int relief = 0)
{
    if (img.isNull()) return img;

    int left = std::max(0, border.left - relief);
    int top = std::max(0, border.top - relief);
    int width = border.right - border.left + (2 * relief);
    int height = border.bottom - border.top + (2 * relief);

    return img.copy(left, top, width, height);
}


inline QRect border_to_qrect(const Border &border, int relief)
{
    return QRect(border.left - relief, 
                 border.top - relief,
                 border.right - border.left + (2 * relief),
                 border.bottom - border.top + (2 * relief));
}


// Create a blank image in the target page that is of the same
// size and format as in the source.
//
// This is to make it easy to have a blank page at the
// end of a document for viewing in 2 page mode with
// odd # of pages. 
//
// returns true if the source image is not null, false otherwise,
// but stil works if source is null, it then just ensures the target
// is also null. 
inline bool copy_blank_image(const Page &source, Page &target)
{
    if (source.img.isNull()) {
        if (!target.img.isNull()) target.img = QImage();
        return false;
    }

    QImage source_image = source.img;
    QImage blank_image(source_image.size(), source.img.format());
    blank_image.setDevicePixelRatio(source.img.devicePixelRatio());
    blank_image.fill(Qt::white);

    target.img = blank_image;
    target.border = source.border;
    target.page_num = source.page_num;
    target.double_page = source.double_page;
    return true;
}



