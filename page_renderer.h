#pragma once

#include <memory>
#include <string>
#include <vector>
#include "page.h"

class Document;

enum class PageRequestType {
    CurrentDisplay,  // For the page being actively displayed
    Prefetch        // For prefetching adjacent pages
};


class PageRenderer {
public:
    PageRenderer(Document* document);

    void replace_document(Document* document) { document_ = document; }

    // ==================== QUERY CURRENT STATE ====================

    // Get the total number of pages (virtual count in performance mode)
    // This is what controls should use for their maximum value
    int page_count() const;

    // Get current 1-based index into the page sequence
    // This is what controls should use for their current value
    int current_index() const;

    // Get display string for current position (e.g., "1", "1a", "2")
    // This is what should be shown in status bars or labels
    std::string current_page_display() const;

    // Get list of ALL page display strings for populating dropdowns
    // In normal mode: ["1", "2", "3", "4"]
    // In performance mode with break on page 1: ["1a", "1b", "2", "3", "4"]
    std::vector<std::string> get_all_page_displays() const;

    // ==================== NAVIGATION ====================

    // Move forward/backward in the sequence
    // Automatically handles segment boundaries in performance mode
    void next();
    void prev();
    bool can_go_next() const;
    bool can_go_prev() const;

    // Go to a specific index in the sequence (1-based)
    // This is what controls call: scrollbar, combobox, etc.
    // Index must be in range [1, page_count()]
    void goto_index(int index);

    // Go to a specific physical page number (1-based, from bookmarks/goto dialog)
    // This maps the physical page to the appropriate index internally
    // If in performance mode and the page has segments, goes to first segment
    void goto_physical_page(int physical_page);

    // ==================== PAGE RENDERING ====================

    // Get the page to display at current position
    // In normal mode: returns the full page
    // In performance mode: returns the cropped page for current segment
    Page get_current_page() const;

    // Get the page at a specific index without changing current position
    // Used for both current display and prefetching
    Page get_page_at_index(int index, PageRequestType request_type = PageRequestType::Prefetch) const;

    // Map an index to physical page and segment (for UI callbacks)
    struct Position {
        int physical_page; // 1-based physical page number
        int segment_index; // 0-based segment within the page
    };
    Position index_to_position(int index) const;

private:
    Document* document_;

    // The single source of truth: current position in the virtual page sequence
    int current_index_;

    // Map a physical page (and optionally segment) to an index
    int position_to_index(int physical_page, int segment_index = 0) const;

    // Get number of segments for a physical page
    // Returns 1 in normal mode, 1+ in performance mode depending on breaks
    int segment_count(int physical_page) const;

    // Get total physical page count
    int physical_page_count() const;

    // Crop a page to a specific segment
    Page crop_page(const Page& page, int physical_page, int segment_index) const;

    // Format display string for a position
    std::string format_display(int physical_page, int segment_index) const;
};
