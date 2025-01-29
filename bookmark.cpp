#include "bookmark.h"
#include <random>
#include <sstream>
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include "logger.h"


Bookmark::Bookmark(std::string title)
    : title_(std::move(title))
    , handle_(generate_uuid())
{}

Bookmark::Bookmark(std::string title, int page_num)
    : title_(std::move(title))
    , handle_(generate_uuid())
{
    if (page_num > 0)
    {
        page_num_ = page_num;
    }  
}

void Bookmark::add_child(const Bookmark &bookmark)
{
    Bookmark child = bookmark;
    child.parent_handle_ = handle_;  // Ensure child correctly tracks parent
    children_.push_back(std::move(child));
}


bool Bookmark::remove_child(const std::string &handle)
{
    for (auto it = children_.begin(); it != children_.end(); ++it)
    {
        if (it->handle_ == handle)
        {
            children_.erase(it);
            return true;
        }
        if (auto removed = it->remove_child(handle))
        {
            return true;
        }
    }
    return false;
}

std::optional<Bookmark> Bookmark::find(const std::string &handle)
{
    if (this->handle_ == handle)
    {
        return *this;
    }
    for (auto &child : children_)
    {
        if (auto result = child.find(handle))
        {
            return result;
        }
    }
    return std::nullopt;
}

bool Bookmark::reparent(std::optional<std::string> new_parent, std::vector<Bookmark> &top_level_bookmarks)
{
    if (parent_handle_)
    {
        // Find the current parent using parent_handle

        for (auto &parent : top_level_bookmarks)
        {
            if (auto old_parent = parent.find(*parent_handle_))
            {
                // Find the actual parent
                if (!old_parent->remove_child(handle_))
                {
                    return false;  // Failed to remove from old parent
                }
                break;
            }
        }
    }

    if (new_parent)
    {
        for (auto &parent : top_level_bookmarks)
        {
            if (parent.handle_ == *new_parent)
            {
                parent.add_child(*this);
                parent_handle_ = parent.handle_;
                return true;
            }
        }
        return false;
    }
    else
    {
        parent_handle_.reset();  // No parent = top-level bookmark
        top_level_bookmarks.push_back(*this);
        return true;
    }
}

std::string Bookmark::generate_uuid()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(0, 15);

    std::ostringstream oss;
    oss << std::hex;
    for (int i = 0; i < 32; ++i)
    {
        oss << dist(gen);
    }
    return oss.str();
}


std::vector<Bookmark> convert_outline_to_bookmarks(fz_outline *outline)
{
    std::vector<Bookmark> bookmarks;

    while (outline)
    {
        std::string title = outline->title ? outline->title : "";  // Ensure valid UTF-8 title

        if (outline->page.chapter != 0)
        {
            logger::log_error("Unexpected non-zero chapter in fz_outline: " + std::to_string(outline->page.chapter));
        }

        Bookmark bookmark(title, outline->page.page);

        if (outline->down)
        {
            auto children = convert_outline_to_bookmarks(outline->down);
            for (auto &child : children)
            {
                bookmark.add_child(child);
                child.parent_handle_ = bookmark.handle_;
            }
        }

        bookmarks.push_back(std::move(bookmark));
        outline = outline->next;  // Move to the next sibling
    }

    return bookmarks;
}
