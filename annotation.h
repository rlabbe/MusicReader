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
};
