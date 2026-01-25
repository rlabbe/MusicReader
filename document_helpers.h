#pragma once
#pragma warning(disable : 4611) // disable warning about _setjump not working with c++ destructors


inline std::string to_string(QImage::Format format)
{
    switch (format) {
        case QImage::Format_Invalid: return "Format_Invalid";
        case QImage::Format_Mono: return "Format_Mono";
        case QImage::Format_MonoLSB: return "Format_MonoLSB";
        case QImage::Format_Indexed8: return "Format_Indexed8";
        case QImage::Format_RGB32: return "Format_RGB32";
        case QImage::Format_ARGB32: return "Format_ARGB32";
        case QImage::Format_ARGB32_Premultiplied: return "Format_ARGB32_Premultiplied";
        case QImage::Format_RGB16: return "Format_RGB16";
        case QImage::Format_ARGB8565_Premultiplied: return "Format_ARGB8565_Premultiplied";
        case QImage::Format_RGB666: return "Format_RGB666";
        case QImage::Format_ARGB6666_Premultiplied: return "Format_ARGB6666_Premultiplied";
        case QImage::Format_RGB555: return "Format_RGB555";
        case QImage::Format_ARGB8555_Premultiplied: return "Format_ARGB8555_Premultiplied";
        case QImage::Format_RGB888: return "Format_RGB888";
        case QImage::Format_RGB444: return "Format_RGB444";
        case QImage::Format_ARGB4444_Premultiplied: return "Format_ARGB4444_Premultiplied";
        case QImage::Format_RGBX8888: return "Format_RGBX8888";
        case QImage::Format_RGBA8888: return "Format_RGBA8888";
        case QImage::Format_RGBA8888_Premultiplied: return "Format_RGBA8888_Premultiplied";
        case QImage::Format_BGR30: return "Format_BGR30";
        case QImage::Format_A2BGR30_Premultiplied: return "Format_A2BGR30_Premultiplied";
        case QImage::Format_RGB30: return "Format_RGB30";
        case QImage::Format_A2RGB30_Premultiplied: return "Format_A2RGB30_Premultiplied";
        case QImage::Format_Alpha8: return "Format_Alpha8";
        case QImage::Format_Grayscale8: return "Format_Grayscale8";
        case QImage::Format_RGBX64: return "Format_RGBX64";
        case QImage::Format_RGBA64: return "Format_RGBA64";
        case QImage::Format_RGBA64_Premultiplied: return "Format_RGBA64_Premultiplied";
        case QImage::Format_Grayscale16: return "Format_Grayscale16";
        case QImage::Format_BGR888: return "Format_BGR888";
        case QImage::Format_RGBX16FPx4: return "Format_RGBX16FPx4";
        case QImage::Format_RGBA16FPx4: return "Format_RGBA16FPx4";
        case QImage::Format_RGBA16FPx4_Premultiplied: return "Format_RGBA16FPx4_Premultiplied";
        case QImage::Format_RGBX32FPx4: return "Format_RGBX32FPx4";
        case QImage::Format_RGBA32FPx4: return "Format_RGBA32FPx4";
        case QImage::Format_RGBA32FPx4_Premultiplied: return "Format_RGBA32FPx4_Premultiplied";
        case QImage::Format_CMYK8888: return "Format_CMYK8888";
        default: return "Unknown Format (" + std::to_string(static_cast<int>(format)) + ")";
    }
}


inline int get_max_screen_height()
{
    static int max_screen_height = []() {
        int max_height = 0;
        const auto screens = QGuiApplication::screens();
        for (const auto* screen : screens) {
            max_height = std::max(max_height, screen->size().height());
        }
        return max_height;
    }();
    return max_screen_height;
}
