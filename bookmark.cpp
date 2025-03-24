#include "bookmark.h"
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include "logger.h"
#include "json.hpp"


Bookmark::Bookmark(std::string title)
    : title_(std::move(title))
    , handle_(generate_uuid())
{}


Bookmark::Bookmark(std::string title, int page_num)
    : title_(std::move(title))
    , handle_(generate_uuid())
{
    if (page_num > 0)
        page_num_ = page_num;
}


void Bookmark::add_child(const Bookmark &bookmark)
{
    Bookmark child = bookmark;
    child.parent_handle_ = handle_;  // Ensure child correctly tracks parent
    children_.push_back(std::move(child));
}


bool Bookmark::remove_child(const BookmarkHandle &handle)
{
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->handle_ == handle) {
            children_.erase(it);
            return true;
        }
        if (auto removed = it->remove_child(handle)) {
            return true;
        }
    }
    return false;
}


Bookmark *Bookmark::find(const BookmarkHandle &handle)
{
    if (this->handle_ == handle) {
        return this;
    }
    for (auto &child : children_) {
        if (auto result = child.find(handle)) {
            return result;
        }
    }
    return nullptr;
}



bool Bookmark::reparent_top(std::vector<Bookmark> &top_level_bookmarks)
{
    return false;
}


bool Bookmark::reparent(const BookmarkHandle &new_parent, std::vector<Bookmark> &top_level_bookmarks)
{
    if (new_parent)
        throw std::invalid_argument("new_parent cannot be NO_HANDLE");

    // first, are we a topmost bookmark?
    if (parent_handle_) {
        // Find the current parent using parent_handle
        for (auto &parent : top_level_bookmarks) {
            if (auto old_parent = parent.find(parent_handle_)) {
                // Find the actual parent
                if (!old_parent->remove_child(handle_)) {
                    return false;  // Failed to remove from old parent
                }
                break;
            }
        }
    }

    if (new_parent) {
        for (auto &parent : top_level_bookmarks) {
            if (parent.handle_ == new_parent) {
                parent.add_child(*this);
                parent_handle_ = parent.handle_;
                return true;
            }
        }
        return false;
    } else {
        parent_handle_.clear();  // No parent = top-level bookmark
        top_level_bookmarks.push_back(*this);
        return true;
    }
}

int Bookmark::generate_uuid()
{
    static int id = 0;
    ++id;
    return id;
}


std::vector<Bookmark> convert_outline_to_bookmarks(fz_outline *outline)
{
    std::vector<Bookmark> bookmarks;

    while (outline) {
        std::string title = outline->title ? outline->title : "";  // Ensure valid UTF-8 title

        if (outline->page.chapter != 0) {
            logger::log_error("Unexpected non-zero chapter in fz_outline: " + std::to_string(outline->page.chapter));
        }

        Bookmark bookmark(title, outline->page.page + 1); // page numbers are 0-based

        if (outline->down) {
            auto children = convert_outline_to_bookmarks(outline->down);
            for (auto &child : children) {
                bookmark.add_child(child);
                child.parent_handle_ = bookmark.handle_;
            }
        }

        bookmarks.push_back(std::move(bookmark));
        outline = outline->next;  // Move to the next sibling
    }

    return bookmarks;
}


std::string to_json(std::vector<Bookmark> &bookmarks) noexcept
{
    try {
        nlohmann::json j;

        std::function<nlohmann::json(const std::vector<Bookmark> &, int)> convert = [&](const std::vector<Bookmark> &bmarks, int level) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto &b : bmarks) {
                nlohmann::json obj;
                obj["title"] = b.title_;
                if (b.page_num_.has_value()) obj["page_num"] = b.page_num_.value();
                obj["children"] = convert(b.children_, level + 1);
                arr.push_back(obj);
            }
            return arr;
        };

        j = convert(bookmarks, 0);
        return j.dump(4);  // Pretty-print with 4 spaces
    } catch (...) {
        return "";
    }
}


std::vector<Bookmark> json_to_bookmark(const std::string &bookmarks)
{
    try {
        auto j = nlohmann::json::parse(bookmarks);

        std::function<std::vector<Bookmark>(const nlohmann::json &)> convert = [&](const nlohmann::json &arr) {
            std::vector<Bookmark> bmarks;
            for (const auto &item : arr) {
                std::string title = item.at("title").get<std::string>();
                std::optional<int> page_num;
                if (item.contains("page_num")) page_num = item.at("page_num").get<int>();

                Bookmark b = page_num.has_value() ? Bookmark(title, page_num.value()) : Bookmark(title);
                if (item.contains("children")) b.children_ = convert(item.at("children"));
                bmarks.push_back(b);
            }
            return bmarks;
        };

        return convert(j);
    } catch (...) {
        return {};
    }
}


std::string as_python_list(const std::vector<Bookmark> &bookmarks)
{
    std::function<std::string(const std::vector<Bookmark> &)> convert;
    convert = [&](const std::vector<Bookmark> &bmarks) -> std::string {
        std::string result = "[";
        bool first = true;
        for (const auto &b : bmarks) {
            if (!first) result += ", ";
            first = false;

            result += "['" + b.title_ + "', " + std::to_string(b.page_num_.value_or(0));
            if (!b.children_.empty()) {
                result += ", " + convert(b.children_);
            }
            result += "]";
        }
        result += "]";
        return result;
    };
    return convert(bookmarks);
}


