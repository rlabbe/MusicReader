#include "imslp_client.h"
#include <iostream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "logger.h"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// Static helper functions
static std::wstring string_to_wstring(const std::string &str)
{
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

static std::string wstring_to_string(const std::wstring &wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

static std::string url_encode(const std::string &str)
{
    std::string encoded;
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += "%20";
        } else {
            char hex[4];
            sprintf_s(hex, "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

static json make_request(HINTERNET hConnect, const std::map<std::string, std::string> &params)
{
    std::string query_string;
    for (const auto &[key, value] : params) {
        if (!query_string.empty()) query_string += "&";
        query_string += key + "=" + url_encode(value);
    }

    std::wstring path = L"/api.php?" + string_to_wstring(query_string);

    logger::debug("Path: {}", wstring_to_string(path));

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        DWORD error = GetLastError();
        logger::error("Failed to open request, error: {}", error);
        throw std::runtime_error("Failed to open request, error: " + std::to_string(error));
    }

    // Enable automatic redirect following
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

    // Set a longer timeout for the request
    DWORD timeout = 30000; // 30 seconds
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (!result) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(hRequest);
        logger::error("Failed to send request, error: {}", error);
        throw std::runtime_error("Failed to send request, error: " + std::to_string(error));
    }

    result = WinHttpReceiveResponse(hRequest, nullptr);
    if (!result) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(hRequest);
        logger::error("Failed to receive response, error: {}", error);
        throw std::runtime_error("Failed to receive response, error: " + std::to_string(error));
    }

    // Check HTTP status
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

    logger::debug("HTTP Status: {}", status_code);

    std::string response_data;
    DWORD bytes_available = 0;
    DWORD bytes_read = 0;
    char buffer[8192];

    do {
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
            break;
        }

        if (bytes_available > 0) {
            DWORD to_read = (std::min)(bytes_available, (DWORD)sizeof(buffer));
            if (WinHttpReadData(hRequest, buffer, to_read, &bytes_read)) {
                response_data.append(buffer, bytes_read);
            }
        }
    } while (bytes_available > 0);

    WinHttpCloseHandle(hRequest);

    logger::debug("Response length: {}", response_data.length());
    if (!response_data.empty()) {
        logger::debug("Response preview: {}", response_data.substr(0, 200));
    }

    if (response_data.empty()) {
        logger::error("Empty response from server");
        throw std::runtime_error("Empty response from server");
    }

    return json::parse(response_data);
}

IMSLPClient::IMSLPClient()
{
    hSession = WinHttpOpen(L"IMSLP Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) {
        logger::error("Failed to open WinHTTP session");
        throw std::runtime_error("Failed to open WinHTTP session");
    }

    hConnect = WinHttpConnect(hSession, L"imslp.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        logger::error("Failed to connect to IMSLP");
        throw std::runtime_error("Failed to connect to IMSLP");
    }

    logger::info("IMSLP client initialized successfully");
}

IMSLPClient::~IMSLPClient()
{
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
}

std::vector<std::string> IMSLPClient::search_works(const std::string &search_term, int namespace_id)
{
    logger::info("Searching for: {}", search_term);

    std::map<std::string, std::string> params = {
        {"action", "query"},
        {"format", "json"},
        {"list", "search"},
        {"srsearch", search_term},
        {"srnamespace", std::to_string(namespace_id)}
    };

    json response = make_request(hConnect, params);
    std::vector<std::string> results;

    if (response.contains("query") && response["query"].contains("search")) {
        for (const auto &item : response["query"]["search"]) {
            results.push_back(item["title"]);
        }
    }

    logger::info("Found {} works", results.size());
    return results;
}

std::vector<FileInfo> IMSLPClient::get_page_files(const std::string &page_title,
                                    const std::vector<std::string> &extensions)
{
    logger::info("Getting files for page: {}", page_title);

    std::map<std::string, std::string> params = {
        {"action", "query"},
        {"format", "json"},
        {"titles", page_title},
        {"prop", "images"}
    };

    json response = make_request(hConnect, params);
    std::vector<FileInfo> files;

    if (!response.contains("query") || !response["query"].contains("pages")) {
        return files;
    }

    for (const auto &[page_id, page_data] : response["query"]["pages"].items()) {
        if (!page_data.contains("images")) continue;

        for (const auto &image : page_data["images"]) {
            std::string title = image["title"];

            bool has_extension = false;
            for (const auto &ext : extensions) {
                if (title.size() >= ext.size() &&
                    title.substr(title.size() - ext.size()) == ext) {
                    has_extension = true;
                    break;
                }
            }

            if (has_extension) {
                auto file_info = get_file_info(title);
                if (!file_info.filename.empty()) {
                    files.push_back(file_info);
                }
            }
        }
    }

    logger::info("Found {} files with matching extensions", files.size());
    return files;
}

FileInfo IMSLPClient::get_file_info(const std::string &file_title, int thumb_width)
{
    std::map<std::string, std::string> params = {
        {"action", "query"},
        {"format", "json"},
        {"titles", file_title},
        {"prop", "imageinfo"},
        {"iiprop", "url|size|mime|thumbmime"},
        {"iiurlwidth", std::to_string(thumb_width)}
    };

    json response = make_request(hConnect, params);
    FileInfo file_info;

    if (!response.contains("query") || !response["query"].contains("pages")) {
        return file_info;
    }

    for (const auto &[page_id, page_data] : response["query"]["pages"].items()) {
        if (page_data.contains("imageinfo") && !page_data["imageinfo"].empty()) {
            const auto &info = page_data["imageinfo"][0];
            file_info.filename = file_title;
            file_info.url = info["url"];
            file_info.size = info["size"];
            file_info.mime = info.value("mime", "");
            file_info.thumb_url = info.value("thumburl", "");
            file_info.thumb_mime = info.value("thumbmime", "");
        }
    }

    return file_info;
}

std::vector<FileInfo> IMSLPClient::get_work_pdfs(const std::string &search_term)
{
    logger::info("Getting PDFs for search term: {}", search_term);
    auto works = search_works(search_term);
    std::vector<FileInfo> all_pdfs;
    for (const auto &work_title : works) {
        logger::info("Getting files for work: {}", work_title);
        auto pdfs = get_page_files(work_title);
        logger::info("Found {} PDFs for work: {}", pdfs.size(), work_title);
        for (const auto &pdf : pdfs) {
            std::cout << "filename: " << pdf.filename << std::endl;
            std::cout << "url: " << pdf.url << std::endl;
            std::cout << "thumb_url: " << pdf.thumb_url << std::endl;
            std::cout << "size: " << pdf.size << std::endl;
            std::cout << "mime: " << pdf.mime << std::endl;
            std::cout << "thumb_mime: " << pdf.thumb_mime << std::endl;
            std::cout << "---" << std::endl;
        }
        all_pdfs.insert(all_pdfs.end(), pdfs.begin(), pdfs.end());
    }
    logger::info("Total PDFs found: {}", all_pdfs.size());
    return all_pdfs;
}

std::vector<uint8_t> IMSLPClient::get_thumbnail_data(const std::string &thumb_url)
{
    logger::debug("Downloading thumbnail: {}", thumb_url);

    // Parse URL to extract host and path
    std::string url = thumb_url;
    if (url.starts_with("//")) {
        url = "https:" + url; // Add protocol for protocol-relative URLs
    }

    // Simple URL parsing - extract host and path
    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos) {
        logger::error("Invalid URL format: {}", thumb_url);
        return {};
    }

    size_t host_start = protocol_pos + 3;
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        logger::error("No path found in URL: {}", thumb_url);
        return {};
    }

    std::string host = url.substr(host_start, path_start - host_start);
    std::string path = url.substr(path_start);

    logger::debug("Host: {}, Path: {}", host, path);

    // Connect to host
    std::wstring whost = string_to_wstring(host);
    std::wstring wpath = string_to_wstring(path);

    HINTERNET hImageConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hImageConnect) {
        DWORD error = GetLastError();
        logger::error("Failed to connect to {}, error: {}", host, error);
        return {};
    }

    HINTERNET hRequest = WinHttpOpenRequest(hImageConnect, L"GET", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        DWORD error = GetLastError();
        logger::error("Failed to open request for thumbnail, error: {}", error);
        WinHttpCloseHandle(hImageConnect);
        return {};
    }

    // Enable automatic redirect following
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

    BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (!result) {
        DWORD error = GetLastError();
        logger::error("Failed to send thumbnail request, error: {}", error);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hImageConnect);
        return {};
    }

    result = WinHttpReceiveResponse(hRequest, nullptr);
    if (!result) {
        DWORD error = GetLastError();
        logger::error("Failed to receive thumbnail response, error: {}", error);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hImageConnect);
        return {};
    }

    // Check HTTP status
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

    logger::debug("Thumbnail HTTP Status: {}", status_code);

    if (status_code != 200) {
        logger::error("Thumbnail request failed with status: {}", status_code);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hImageConnect);
        return {};
    }

    // Read image data
    std::vector<uint8_t> image_data;
    DWORD bytes_available = 0;
    DWORD bytes_read = 0;
    char buffer[8192];

    do {
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
            break;
        }

        if (bytes_available > 0) {
            DWORD to_read = (std::min)(bytes_available, (DWORD)sizeof(buffer));
            if (WinHttpReadData(hRequest, buffer, to_read, &bytes_read)) {
                image_data.insert(image_data.end(), buffer, buffer + bytes_read);
            }
        }
    } while (bytes_available > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hImageConnect);

    logger::debug("Downloaded {} bytes of thumbnail data", image_data.size());
    return image_data;
}