// bookmark.h
#pragma once

#include <string>
#include <vector>
#include <optional>

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
    bool remove_child(const std::string &handle);

    // Recursively searches for a bookmark by handle.
    //
    // Parameters:
    // handle - The unique handle of the bookmark to find.
    //
    // Returns:
    // The found Bookmark object if it exists, otherwise std::nullopt.
    std::optional<Bookmark> find(const std::string &handle);

    // Moves this bookmark to a new parent or to the top level.
    //
    // Parameters:
    // new_parent - The new parent bookmark handle, or std::nullopt if moving to top level.
    // top_level_bookmarks - The list of top-level bookmarks in the document.
    //
    // Returns:
    // True if reparenting was successful, False otherwise.
    bool reparent(std::optional<std::string> new_parent, std::vector<Bookmark> &top_level_bookmarks);

private:
    // Generates a unique handle for bookmarks.
    //
    // Returns:
    // A unique string that serves as the bookmark's handle.
    static std::string generate_uuid();

public:
    // The UTF-8 encoded title of the bookmark.
    std::string title_;

    // The page number this bookmark points to (if applicable).
    std::optional<int> page_num_;

    // List of child bookmarks, used for nested structures.
    std::vector<Bookmark> children_;

    // A unique handle identifying this bookmark.
    std::string handle_;

    // Handle to the parent bookmark, if any (used for nesting).
    std::optional<std::string> parent_handle_;
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

