#pragma once

#include <QPixmap>
#include <QImage>
#include <QSize>
#include <algorithm>
#include "border.h"
#include "logger.h"
#include "performance_data.h"

QPixmap resize_by_border(const QPixmap& img, const Border& border, int relief);
QImage resize_by_border(const QImage& img, const Border& border, int relief);


// This is used to store a page as a QImage. The QImage can be of any format,
// such as RGB, RGBA, etc. Most music is grayscale or mono, so this lets
// us use a smaller image format. PixmapPage below is used by the UI to
// display the page.
struct Page {
    QImage img;
    int page_num {1};
    Border border;
    bool double_page {false}; // use if constructed from 2 pages for viewing

    Page() {}
    explicit Page(int num)
        : page_num(num)
    {
    }

    Page(const QImage& image, int page_number, bool doubled)
        : img(image)
        , page_num(page_number)
        , double_page(doubled)
    {
        border = find_content_edges(img);
    }

    Page(const QImage& image, const Border& precomputed_border, int page_number, bool doubled)
        : img(image)
        , page_num(page_number)
        , border(precomputed_border)
        , double_page(doubled)
    {
    }

    bool is_empty() const { return img.isNull(); }
    QSize shape() const { return img.isNull() ? QSize() : img.size(); }
    QSize size() const { return shape(); }
    int width() const { return shape().width(); }
    int height() const { return shape().height(); }

    QPixmap as_pixmap() const { return as_pixmap(img); }

    static QPixmap as_pixmap(const QImage& image)
    {
        TRACE_CALL;
        if (image.isNull())
            return QPixmap();
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(image.devicePixelRatio());
        return pixmap;
    }


    QImage resize_by_border(int relief = 0)
    {
        TRACE_CALL;
        return ::resize_by_border(img, border, relief);
    }
};


struct PixmapPage {
    QPixmap pixmap;
    int page_num {1};
    Border border;
    bool double_page {false};

    PixmapPage() {}

    PixmapPage(const Page& page)
        : pixmap(Page::as_pixmap(page.img))
        , page_num(page.page_num)
        , border(page.border)
        , double_page(page.double_page)
    {
    }

    PixmapPage(const QImage& image, int page_number, bool doubled)
        : pixmap(Page::as_pixmap(image))
        , page_num(page_number)
        , double_page(doubled)
    {
        border = find_content_edges(image);
    }

    PixmapPage(const QPixmap& pix, int page_number, bool doubled)
        : pixmap(pix)
        , page_num(page_number)
        , double_page(doubled)
    {
        border = find_content_edges(pix.toImage());
    }

    bool is_empty() const { return pixmap.isNull(); }
    QSize shape() const { return pixmap.isNull() ? QSize() : pixmap.size(); }
    QSize size() const { return shape(); }
    int width() const { return shape().width(); }
    int height() const { return shape().height(); }

    QPixmap as_pixmap() const { return pixmap; }

    QPixmap resize_by_border(int relief = 0) const
    {
        TRACE_CALL;
        return ::resize_by_border(pixmap, border, relief);
    }
};


inline QPixmap resize_by_border(const QPixmap& img, const Border& border, int relief = 0)
{
    if (img.isNull())
        return img;

    int left = std::max(0, border.left - relief);
    int top = std::max(0, border.top - relief);
    int width = border.right - border.left + (2 * relief);
    int height = border.bottom - border.top + (2 * relief);

    // Crop the image based on the border
    return img.copy(left, top, width, height);
}


inline QImage resize_by_border(const QImage& img, const Border& border, int relief = 0)
{
    if (img.isNull())
        return img;

    int left = std::max(0, border.left - relief);
    int top = std::max(0, border.top - relief);
    int width = border.right - border.left + (2 * relief);
    int height = border.bottom - border.top + (2 * relief);

    return img.copy(left, top, width, height);
}


inline QRect border_to_qrect(const Border& border, int relief)
{
    return QRect(border.left - relief, border.top - relief, border.right - border.left + (2 * relief),
                 border.bottom - border.top + (2 * relief));
}


// Override the top and/or bottom of an auto-detected border with a user-defined
// paper crop. page_img_height_pixels MUST be the height in pixels of the
// rendered QImage the border was computed from (same pixel space). Left/right
// come through unchanged. If the crop sets only one side, the other side
// retains its auto-detected value.
inline Border apply_paper_crop(const Border& b, const PaperCrop& crop, int page_img_height_pixels)
{
    Border result = b;
    if (crop.top) {
        int y = static_cast<int>(*crop.top * page_img_height_pixels);
        result.top = std::clamp(y, 0, page_img_height_pixels);
    }
    if (crop.bottom) {
        int y = static_cast<int>(*crop.bottom * page_img_height_pixels);
        result.bottom = std::clamp(y, 0, page_img_height_pixels);
    }
    return result;
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
inline bool copy_blank_image(const Page& source, Page& target)
{
    if (source.img.isNull()) {
        if (!target.img.isNull())
            target.img = QImage();
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


// Overload for PixmapPage
inline bool copy_blank_image(const PixmapPage& source, PixmapPage& target)
{
    if (source.pixmap.isNull()) {
        if (!target.pixmap.isNull())
            target.pixmap = QPixmap();
        return false;
    }

    QPixmap blank_pixmap(source.pixmap.size());
    blank_pixmap.setDevicePixelRatio(source.pixmap.devicePixelRatio());
    blank_pixmap.fill(Qt::white);

    target.pixmap = blank_pixmap;
    target.border = source.border;
    target.page_num = source.page_num;
    target.double_page = source.double_page;
    return true;
}
