#include "border.h"


#include <iostream>

Border find_content_edges(const QImage &img, int black_pixel_threshold)
{
    if (img.isNull()) {
        return Border{ 0, 0, 0, 0 };  // Handle empty images
    }

    // Convert to grayscale if needed
    QImage grayscale = img;
    if (img.format() != QImage::Format_Grayscale8) {
        grayscale = img.convertToFormat(QImage::Format_Grayscale8);
    }

    int width = grayscale.width();
    int height = grayscale.height();
    const uchar *data = grayscale.bits();
    int bytes_per_line = grayscale.bytesPerLine();

    // Apply binary thresholding: everything above 127 becomes 255 (white), below becomes 0 (black)
    QImage binary(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        const uchar *src = data + y * bytes_per_line;
        uchar *dst = binary.scanLine(y);
        for (int x = 0; x < width; ++x) {
            dst[x] = (src[x] > 172) ? 255 : 0;
        }
    }

    const uchar *binary_data = binary.bits();
    int binary_stride = binary.bytesPerLine();

    auto count_black_pixels = [&](const uchar *line, int length) -> int {
        return std::count(line, line + length, 0);
    };

    int top = 0, bottom = height, left = 0, right = width;

    std::vector<int> counts;
    counts.reserve(std::max(width, height));

    // maybe an odd algorithm, but, find first significant black grouping of pixels
    // on a scan line. Then, back up and if previous lines also had black, keep going
    // Needs to be replaced with connected components or something.

    // Find top edge
    for (int i = 0; i < height; ++i) {
        int count = count_black_pixels(binary_data + i * binary_stride, width);
        if (count > black_pixel_threshold) {
            top = i;
            break;
        }
        counts.push_back(count);
    }

    while (counts.back() > 1) {
        --top;
        counts.pop_back();
    }
    counts.clear();

    // Find bottom edge
    for (int i = height - 1; i >= 0; --i) {
        int count = count_black_pixels(binary_data + i * binary_stride, width);
        if (count > black_pixel_threshold) {
            bottom = i;
            break;
        }
        counts.push_back(count);
    }
    while (counts.back() > 1) {
        ++bottom;
        counts.pop_back();
    }
    counts.clear();

    // Find left edge
    for (int i = 0; i < width; ++i) {
        int count = 0;
        for (int j = 0; j < height; ++j) {
            if (binary_data[j * binary_stride + i] == 0) {
                count++;
            }
        }
        if (count > black_pixel_threshold) {
            left = i;
            break;
        }
        counts.push_back(count);

    }
    while (counts.back() > 1) {
        --left;
        counts.pop_back();
    }
    counts.clear();

    // Find right edge
    for (int i = width - 1; i >= 0; --i) {
        int count = 0;
        for (int j = 0; j < height; ++j) {
            if (binary_data[j * binary_stride + i] == 0) {
                count++;
            }
        }
        if (count > black_pixel_threshold) {
            right = i;
            break;
        }
        counts.push_back(count);
    }
    while (counts.back() > 1) {
        ++right;
        counts.pop_back();
    }

    //img.save("img.bmp");
    //grayscale.save("grayscale.bmp");
    //binary.save("binary.bmp");

    // Don't allow "too" small
    double max_shrink_ratio = 0.20;
    double max_width = width * (1.0 - max_shrink_ratio);
    double max_height = height * (1.0 - max_shrink_ratio);
    double my_width = right - left;
    double my_height = bottom - top;

    if (my_width >= max_width && my_height >= max_height)
        return Border{ .top = top, .bottom = bottom, .left = left, .right = right };
    else
        return Border{ .top = 0, .bottom = height, .left = 0, .right = width };
}
