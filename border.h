#pragma once

#include <algorithm>
#include <QPixmap>
#include <QRect>

struct Border
{
    int top{ 0 };
    int bottom{ 0 };
    int left{ 0 };
    int right{ 0 };

    Border adjust(const QPixmap &img, int top_margin, int side_margin) const
    {
        if (top_margin == 0 && side_margin == 0)
        {
            return *this;
        }

        int width = img.width();
        int height = img.height();

        int new_top = std::max(0, top - top_margin);
        int new_bottom = std::min(height, bottom + top_margin);
        int new_left = std::max(0, left - side_margin);
        int new_right = std::min(width, right + side_margin);

        return Border{ new_top, new_bottom, new_left, new_right };
    }

    static Border from_qrect(const QRect &qrect)
    {
        return Border{ qrect.top(), qrect.bottom(), qrect.left(), qrect.right() };
    }
};



Border find_content_edges(const QImage &img, int black_pixel_threshold = 10);
inline Border find_content_edges(const QPixmap &img, int black_pixel_threshold = 10)
{
    return find_content_edges(img.toImage(), black_pixel_threshold);
}

