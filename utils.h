#pragma once

#include <windows.h>
#include <string>


inline std::string wide_to_utf8(const std::wstring &wstr)
{
    if (wstr.empty()) return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return std::string();

    std::string str(size_needed - 1, 0); // -1 to remove null terminator
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, NULL, NULL);

    return str;
}


