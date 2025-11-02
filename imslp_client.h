#pragma once

#include <string>
#include <vector>
#include <map>
#include <windows.h>
#include <winhttp.h>

struct FileInfo {
    std::string filename;
    std::string url;
    std::string thumb_url;
    long size = 0;
    std::string mime;
    std::string thumb_mime;
};

class IMSLPClient {
private:
    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;

public:
    IMSLPClient();
    ~IMSLPClient();

    std::vector<std::string> search_works(const std::string& search_term, int namespace_id = 0);

    std::vector<FileInfo> get_page_files(const std::string& page_title,
                                         const std::vector<std::string>& extensions = {".pdf"});

    FileInfo get_file_info(const std::string& file_title, int thumb_width = 200);
    std::vector<FileInfo> get_work_pdfs(const std::string& search_term);
    std::vector<uint8_t> get_thumbnail_data(const std::string& thumb_url);

    HINTERNET getSession() const { return hSession; }
};