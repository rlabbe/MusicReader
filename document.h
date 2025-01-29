#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <vector>
#include <memory>
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <QPixmap>
#include "page.h"
#include "bookmark.h"

class Document
{
public:
    Document(std::filesystem::path filename, int dpi);

    std::string filename() const { return filename_.string(); }

    int page_count() const { return static_cast<int>(pages_.size()); }
    Page get_page(int page_num) const;
    bool save(const std::filesystem::path &filename);

    bool can_undo() const { return false; }
    bool can_redo() const { return false; }
    void undo() {}
    void redo() {}


    Bookmark *find_bookmark(const std::string &handle);

    bool reparent_bookmark(const std::string &handle,
                           const std::string &parent_handle,
                           bool internal_call = false);

    bool indent_bookmark(const std::string &handle);
    bool unindent_bookmark(const std::string &handle);

    void rename_bookmark(const std::string &handle,
                         const std::string &title);

    void remove_bookmark(const std::string handle);

    Bookmark add_bookmark(const std::string &title,
                          int page_num);

    std::vector<Bookmark> &bookmarks() { return bookmarks_; }
private:

    void load_document();

    std::vector<Bookmark> bookmarks_;
    std::filesystem::path filename_;

    int dpi_;
    std::vector<Page> pages_;
};

