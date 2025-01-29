#include "border.h"



Border find_content_edges(const QImage &img, int black_pixel_threshold)
{
    if (img.isNull())
    {
        return Border{ 0, 0, 0, 0 };  // Handle empty images
    }

    // Convert to grayscale if needed
    QImage grayscale = img;
    if (img.format() != QImage::Format_Grayscale8)
    {
        grayscale = img.convertToFormat(QImage::Format_Grayscale8);
    }

    int width = grayscale.width();
    int height = grayscale.height();
    const uchar *data = grayscale.bits();
    int bytes_per_line = grayscale.bytesPerLine();

    // Apply binary thresholding: everything above 127 becomes 255 (white), below becomes 0 (black)
    QImage binary(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y)
    {
        const uchar *src = data + y * bytes_per_line;
        uchar *dst = binary.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            dst[x] = (src[x] > 127) ? 255 : 0;
        }
    }

    const uchar *binary_data = binary.bits();
    int binary_stride = binary.bytesPerLine();

    auto count_black_pixels = [&](const uchar *line, int length) -> int
    {
        return std::count(line, line + length, 0);
    };

    int top = 0, bottom = height, left = 0, right = width;

    // Find top edge
    for (int i = 0; i < height; ++i)
    {
        if (count_black_pixels(binary_data + i * binary_stride, width) > black_pixel_threshold)
        {
            top = i;
            break;
        }
    }

    // Find bottom edge
    for (int i = height - 1; i >= 0; --i)
    {
        if (count_black_pixels(binary_data + i * binary_stride, width) > black_pixel_threshold)
        {
            bottom = i;
            break;
        }
    }

    // Find left edge
    for (int i = 0; i < width; ++i)
    {
        int count = 0;
        for (int j = 0; j < height; ++j)
        {
            if (binary_data[j * binary_stride + i] == 0)
            {
                count++;
            }
        }
        if (count > black_pixel_threshold)
        {
            left = i;
            break;
        }
    }

    // Find right edge
    for (int i = width - 1; i >= 0; --i)
    {
        int count = 0;
        for (int j = 0; j < height; ++j)
        {
            if (binary_data[j * binary_stride + i] == 0)
            {
                count++;
            }
        }
        if (count > black_pixel_threshold)
        {
            right = i;
            break;
        }
    }

    return Border{ top, bottom, left, right };
}
