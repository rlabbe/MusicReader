// bookmark.h
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <assert.h>


class BookmarkHandle {
public:
    static constexpr inline int NO_HANDLE = -1;

private:
    int handle_ = -1;

public:
    BookmarkHandle() = default;
    explicit BookmarkHandle(int h) : handle_(h) 
    {
        assert(h != NO_HANDLE);
    }

    BookmarkHandle &operator=(int h)
    {
        handle_ = h;
        return *this;
    }

    operator int() const { return handle_; }
    operator bool() const { return handle_ != NO_HANDLE; }
    void clear() { handle_ = NO_HANDLE; }
};

inline bool operator==(const BookmarkHandle &lhs, const BookmarkHandle &rhs)
{
    return static_cast<int>(lhs) == static_cast<int>(rhs);
}


// A class representing a PDF bookmark.
class Bookmark
{
public:
    // Constructor for a folder-style bookmark (no page number).
    //
    // Parameters:
    // title - The UTF-8 encoded title of the bookmark.
    explicit Bookmark(std::string title);

    // Constructor for a bookmark with an associated page number.
    //
    // Parameters:
    // title - The UTF-8 encoded title of the bookmark.
    // page_num - The page number this bookmark points to. (0 means no page number)
    Bookmark(std::string title, int page_num);

    // Adds a child bookmark and sets its parent reference.
    //
    // Parameters:
    // bookmark - The child bookmark to add.
    void add_child(const Bookmark &bookmark);

    // Recursively removes a child bookmark by handle and returns it.
    //
    // Parameters:
    // handle - The unique handle of the bookmark to remove.
    //
    // Returns:
    // True if the bookmark was removed, False if not found.
    bool remove_child(const BookmarkHandle &handle);

    // Searches for a bookmark by handle.
    //
    // Parameters:
    // handle - The unique handle of the bookmark to find.
    //
    // Returns:
    // The found Bookmark object if it exists, otherwise std::nullopt.
    Bookmark *find(const BookmarkHandle &handle);


    // Moves this bookmark to a new parent or to the top level.
    //
    // Parameters:
    // new_parent - The new parent bookmark handle, or std::nullopt if moving to top level.
    // top_level_bookmarks - The list of top-level bookmarks in the document.
    //
    // Returns:
    // True if reparenting was successful, False otherwise.
    bool reparent(const BookmarkHandle& parent_handle, std::vector<Bookmark> &top_level_bookmarks);
    bool reparent_top(std::vector<Bookmark> &top_level_bookmarks);

private:
    // Generates a unique handle for bookmarks.
    //
    // Returns:
    // A unique string that serves as the bookmark's handle.
    int generate_uuid();

public:
    // The UTF-8 encoded title of the bookmark.
    std::string title_;

    // The page number this bookmark points to (if applicable).
    std::optional<int> page_num_;

    // List of child bookmarks, used for nested structures.
    std::vector<Bookmark> children_;

    // A unique handle identifying this bookmark.
    BookmarkHandle handle_;

    // Handle to the parent bookmark, if any (used for nesting).
    BookmarkHandle parent_handle_;
};


// Recursively converts an fz_outline structure into a Bookmark object.
//
// Parameters:
// outline - A pointer to the fz_outline structure to convert.
//
// Returns:
// A vector of Bookmark objects representing the outline structure.
struct fz_outline;
std::vector<Bookmark> convert_outline_to_bookmarks(fz_outline *outline);


std::string to_json(std::vector<Bookmark> &bookmarks) noexcept;
std::vector<Bookmark> json_to_bookmark(const std::string &bookmarks);

// Converts a vector of Bookmark objects to a Python list string to be
// used by an external python utility to write bookmarks.
std::string as_python_list(const std::vector<Bookmark> &bookmarks);


inline bool bookmark_sort(const Bookmark &a, const Bookmark &b)
{
    bool a_is_folder = !a.page_num_.has_value();
    bool b_is_folder = !b.page_num_.has_value();

    if (a_is_folder != b_is_folder) {
        return !a_is_folder;  // Bookmarks with pages come first
    }
    if (!a_is_folder && !b_is_folder) {
        return a.page_num_.value() < b.page_num_.value();  // Compare page numbers
    }
    return false;  // Both are folders, maintain insertion order
}
