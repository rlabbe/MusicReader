#include "qpdf_document.h"
#include <filesystem>
#include <iostream>
#include <format>

#include <cstdio>
#include <stdexcept>

#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#ifdef NDEBUG
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include "logger.h"

using namespace std::string_literals;
namespace fs = std::filesystem;



FILE *fopen_with_delete_share(const char *filename, const char *mode)
{
    // Convert mode string to appropriate access flags
    DWORD access = 0;
    DWORD creation = OPEN_EXISTING;

    if (std::strcmp(mode, "rb") == 0) {
        access = GENERIC_READ;
    } else if (std::strcmp(mode, "wb") == 0) {
        access = GENERIC_WRITE;
        creation = CREATE_ALWAYS;
    } else if (std::strcmp(mode, "r+b") == 0) {
        access = GENERIC_READ | GENERIC_WRITE;
    } else if (std::strcmp(mode, "w+b") == 0) {
        access = GENERIC_READ | GENERIC_WRITE;
        creation = CREATE_ALWAYS;
    } else {
        throw std::invalid_argument("Unsupported mode");
    }

    // Open file with FILE_SHARE_DELETE
    HANDLE h = CreateFileA(filename, access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open file with delete sharing");
    }

    // Convert HANDLE to FILE*
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_BINARY);
    if (fd == -1) {
        CloseHandle(h);
        throw std::runtime_error("Failed to open OS file handle");
    }

    FILE *file = _fdopen(fd, mode);
    if (!file) {
        CloseHandle(h);
        throw std::runtime_error("Failed to associate file descriptor with FILE*");
    }

    return file;
}


class FileHandle {
public:
    // Constructor: Opens a file with the given mode
    FileHandle(const char *filename, const char *mode)
    {
        file_ = std::fopen(filename, mode);
        if (!file_) {
            throw std::runtime_error("Failed to open "s + filename);
        }
    }

    ~FileHandle()
    {
        close();
    }


    // Disable copy construction and assignment to avoid ownership issues
    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    // Move constructor
    FileHandle(FileHandle &&other) noexcept : file_(other.file_)
    {
        other.file_ = nullptr;
    }

    // Move assignment
    FileHandle &operator=(FileHandle &&other) noexcept
    {
        if (this != &other) {
            close();
            file_ = other.file_;
            other.file_ = nullptr;
        }
        return *this;
    }

    // Provides access to the underlying FILE* for I/O operations
    FILE *get() const noexcept { return file_; }


    // Explicit close method
    void close() noexcept
    {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

private:
    FILE *file_;
};


std::vector<QPDFObjectHandle> process_outline_level(QPDF &qpdf,
                                                    const std::vector<Bookmark> &bookmarks,
                                                    const QPDFObjectHandle &parent,
                                                    const std::vector<std::pair<int, int>> &page_refs)
{
    std::vector<QPDFObjectHandle> outline_list;
    int prev_id = -1, prev_gen = -1;
    for (const auto &bm : bookmarks) {
        // Create a new stream for this bookmark.
        auto bm_stream = qpdf.newStream();
        auto bm_dict = QPDFObjectHandle::newDictionary();

        bm_dict.replaceKey("/Title"s, QPDFObjectHandle::newString(bm.title_));
        bm_dict.replaceKey("/Parent"s, parent.getObj());

        if (bm.page_num_ && bm.page_num_.value() > 0 &&
            bm.page_num_.value() <= static_cast<int>(page_refs.size())) {
            int idx = bm.page_num_.value() - 1;
            auto dest_arr = QPDFObjectHandle::newArray();
            dest_arr.appendItem(qpdf.getObject(page_refs[idx].first, page_refs[idx].second));
            dest_arr.appendItem(QPDFObjectHandle::newName("/Fit"s));
            bm_dict.replaceKey("/Dest"s, dest_arr);
        }

        // Process children recursively.
        if (!bm.children_.empty()) {
            auto child_outlines = process_outline_level(qpdf, bm.children_, bm_stream, page_refs);
            if (!child_outlines.empty()) {
                bm_dict.replaceKey("/First"s, child_outlines.front());
                bm_dict.replaceKey("/Last"s, child_outlines.back());
                bm_dict.replaceKey("/Count"s, QPDFObjectHandle::newInteger(static_cast<int>(child_outlines.size())));
            }
        }
        // Link siblings.
        if (prev_id > -1 && prev_gen > -1) {
            bm_dict.replaceKey("/Prev"s, qpdf.getObject(prev_id, prev_gen));
            auto prev_obj = qpdf.getObject(prev_id, prev_gen);
            prev_obj.replaceKey("/Next"s, qpdf.getObject(bm_stream.getObjectID(), bm_stream.getGeneration()));
        }
        prev_id = bm_stream.getObjectID();
        prev_gen = bm_stream.getGeneration();

        // Replace the stream object with our completed dictionary.
        qpdf.replaceObject(bm_stream.getObjectID(), bm_stream.getGeneration(), bm_dict);
        // Push the dictionary (which is now the indirect representation) onto our list.
        outline_list.push_back(bm_dict);
    }
    return outline_list;
}




bool copy_pdf_with_bookmarks(const std::string &input_filename,
                             const std::string &output_filename,
                             const std::vector<Bookmark> &bookmarks)
{
    QPDF qpdf;
    FileHandle input_file(input_filename.c_str(), "rb");

    try {

        qpdf.processFile(input_filename.c_str(), input_file.get(), false);
        //input_file.close();


        // Build page reference vector.
        std::vector<std::pair<int, int>> page_refs;
        QPDFPageDocumentHelper page_helper(qpdf);
        for (auto &page : page_helper.getAllPages()) {
            auto handle = page.getObjectHandle();
            page_refs.push_back({ handle.getObjectID(), handle.getGeneration() });
        }

        auto qpdf_root = qpdf.getRoot();
        auto outlines = QPDFObjectHandle::newDictionary();
        outlines.replaceKey("/Type"s, QPDFObjectHandle::newName("/Outlines"s));

        // Process top-level bookmarks recursively.
        auto top_outlines = process_outline_level(qpdf, bookmarks, outlines, page_refs);
        if (!top_outlines.empty()) {
            outlines.replaceKey("/First", top_outlines.front());
            outlines.replaceKey("/Last", top_outlines.back());
            outlines.replaceKey("/Count", QPDFObjectHandle::newInteger(static_cast<int>(top_outlines.size())));
        }

        // Force 'outlines' to be an indirect object.
        {
            auto tmp = qpdf.newStream();
            qpdf.replaceObject(tmp.getObjectID(), tmp.getGeneration(), outlines);
            outlines = qpdf.getObject(tmp.getObjectID(), tmp.getGeneration());
        }

        qpdf_root.replaceKey("/Outlines", outlines.getObj());
    } catch (const std::exception &e) {
        logger::log_error(std::format("Error processing PDF: {}", e.what()));
        return false;
    }
    input_file.close();


    try {
        FileHandle input_file(input_filename.c_str(), "wb");
        QPDFWriter writer(qpdf, input_filename.c_str(), input_file.get(), false);
        writer.setStaticID(false);

        try {
            writer.write();
        } catch (const std::exception &e) {
            logger::log_error(std::format("Error writing PDF: {}", e.what()));
            return false;
        }

    } catch (const std::exception &e) {
        logger::log_error(std::format("Error making QPDFWriter: {}", e.what()));
        return false;
    }
    return true;
}




// Wrapper that writes to a temporary file then renames it over the original.
bool add_bookmarks_to_pdf(const std::string &filename,
                          const std::vector<Bookmark> &bookmarks)
{
    /*std::string temp_filename = filename + ".tmp.pdf";
    try {
        fs::copy(filename, temp_filename, std::filesystem::copy_options::overwrite_existing);
    } catch (const fs::filesystem_error &e) {
        logger::log_error(std::format("Error copying file: {} {}", temp_filename, e.what()));
        return false;
    }*/

    copy_pdf_with_bookmarks(filename, filename, bookmarks);

    /*    try {
            fs::remove(temp_filename);
        } catch (const fs::filesystem_error &e) {
            logger::log_error(std::format("Error removing temporary file: {} {}", temp_filename, e.what()));
            return false;
        }*/
    return true;
}
#else
// no qpdf in debug mode :<

extern bool add_bookmarks_to_pdf(const std::string &,
                                 const std::vector<Bookmark> &)
{
    return false;
}
#endif

