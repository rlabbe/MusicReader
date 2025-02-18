#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <vector>
#include <future>
#include <mutex>
#include <memory>
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include <QPixmap>
#include "page.h"
#include "bookmark.h"


struct fz_context;
struct fz_document;

class Document {
public:
    Document(std::filesystem::path filename, int dpi);

    std::string filename() const { return filename_.string(); }

    int page_count() const { return static_cast<int>(pages_.size()); }
    Page get_page(int page_num) const;
    bool save(const std::filesystem::path &filename = "", bool block = true);

    bool can_undo() const { return false; }
    bool can_redo() const { return false; }
    void undo() {}
    void redo() {}


    bool reparent_bookmark(const BookmarkHandle &handle, const BookmarkHandle &new_parent_handle);

    bool indent_bookmark(const BookmarkHandle &handle);
    bool unindent_bookmark(const BookmarkHandle &handle);

    bool rename_bookmark(const BookmarkHandle &handle,
                         const std::string &title);

    bool remove_bookmark(const BookmarkHandle &handle);

    // Creates bookmark; bool is for whether the save worked or not, not
    // whether the bookmark was added
    std::pair<BookmarkHandle, bool> add_bookmark(const std::string &title,
                                                 int page_num);

    std::pair<BookmarkHandle, bool> add_bookmark(const std::string &title,
                                                 int page_num,
                                                 const BookmarkHandle &parent_handle);

    std::vector<Bookmark> &bookmarks() { return bookmarks_; }
private:

    bool reparent_bookmark(Bookmark bookmark,
                           const BookmarkHandle &new_parent_handle,
                           bool internal_call);

    Bookmark *find_bookmark(const BookmarkHandle &handle);

    void save_annotations(fz_context *, fz_document *);

    void clear_completed_features();

    void load_document();

    std::vector<Bookmark> bookmarks_;
    std::filesystem::path filename_;

    int dpi_;
    std::vector<Page> pages_;

    std::vector<std::vector<Bookmark>> undo_stack_;
    std::vector<std::vector<Bookmark>> redo_stack_;

    // saves are async for performance, save the futures here
    std::vector<std::future<void>> save_futures_;
    std::recursive_mutex save_mutex_;
};

