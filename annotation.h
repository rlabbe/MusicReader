#pragma once

#include <string>
#include <vector>
#include "font_info.h"


class AnnotationHandle {
public:
    static constexpr inline int NO_HANDLE = -1;

private:
    int handle_ = -1;

public:
    AnnotationHandle() = default;
    explicit AnnotationHandle(int h)
        : handle_(h)
    {
    }

    AnnotationHandle& operator=(int h)
    {
        handle_ = h;
        return *this;
    }

    operator int() const { return handle_; }
    operator bool() const { return handle_ != NO_HANDLE; }
    void clear() { handle_ = NO_HANDLE; }
};

inline bool operator==(const AnnotationHandle& lhs, const AnnotationHandle& rhs)
{
    return static_cast<int>(lhs) == static_cast<int>(rhs);
}


class Annotation {
public:
    Annotation(const std::string& text,
               int page_num,
               float x,
               float y,
               float width,
               float height,
               const FontInfo& font_info);

private:
    int generate_uuid();

public:
    AnnotationHandle handle_;
    int page_num_;
    float x_, y_;
    std::string text_;
    FontInfo font_info_;
    float width_, height_;
    bool visible_ = true;
    // Music-symbol annotations are placed/edited via the Bravura SMuFL path
    // (add_freetext_with_custom_font) rather than the standard Base-14 FreeText
    // writer. symbol_codepoint_ is the single SMuFL PUA codepoint (e.g. U+E262
    // for sharp). Reset to false / 0 for normal text annotations.
    bool is_music_symbol_ = false;
    int symbol_codepoint_ = 0;
    // Top edge of the on-disk /Rect in PDF Y-up coordinates. For music symbols
    // we hit-test and draw the selection box using this rect directly (same as
    // Foxit/Acrobat) instead of synthesising bounds from baseline + per-glyph
    // metrics, which can diverge from the saved /Rect across versions.
    float rect_top_pdf_ = 0.0f;
};
