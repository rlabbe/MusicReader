#pragma once

#include <memory>
#include <string>
#include <vector>
#include "page.h"

class Document;

// PageRenderer: The single source of truth for page navigation state and logic.
//
// DESIGN PHILOSOPHY:
// The renderer abstracts ALL navigation complexity. External code (PDFViewer, UI controls)
// should never need to know about:
// - Performance mode vs normal mode
// - Physical pages vs virtual pages
// - Page breaks or segments
// - How many segments exist on any page
//
// External code only needs to know:
// - "How many pages are there?" (could be 4 in normal mode, 5 in performance mode)
// - "What's the current page index?" (0-based index into total count)
// - "What should I display for this position?" (e.g., "1", "1a", "1b", "2")
// - "Navigate forward/backward" or "Go to index N"
//
// USAGE PATTERNS:
//
// 1. SCROLLBAR:
//    - scrollbar->setMaximum(renderer.page_count() - 1)
//    - scrollbar->setValue(renderer.current_index())
//    - On scrollbar change: renderer.goto_index(value); viewer.refresh()
//    - The scrollbar doesn't know if index 1 is "page 2" or "page 1b"
//
// 2. COMBOBOX (toolbar):
//    - Populate: for (auto& display : renderer.get_all_page_displays()) combobox->addItem(display)
//    - Current selection: combobox->setCurrentIndex(renderer.current_index())
//    - On selection: renderer.goto_index(index); viewer.refresh()
//
// 3. KEYBOARD (page up/down):
//    - renderer.next(); viewer.refresh()
//    - renderer.prev(); viewer.refresh()
//
// 4. BOOKMARKS (goto physical page):
//    - renderer.goto_physical_page(bookmark.page_num); viewer.refresh()
//    - The renderer maps the physical page to the correct index internally
//
// 5. DISPLAYING THE PAGE:
//    - Page page = renderer.get_current_page()
//    - The returned page is already cropped/modified as needed
//
// 6. SHOWING PAGE NUMBER IN STATUS BAR:
//    - QString display = renderer.current_page_display()
//    - Shows "1", "1a", "2", etc. as appropriate
//
// STATE MANAGEMENT:
// The renderer maintains a single "current position" which is an index (0-based)
// into the virtual page list. Internally it tracks:
// - current_index_: The 0-based position in the virtual page sequence
// - Physical page mapping is computed on-demand when needed
//
// MODE CHANGES:
// When performance mode is toggled, the page count changes but we try to stay
// on the same physical page. Example:
// - Normal mode: on page 2 (index 1)
// - Switch to performance mode (page 1 has 2 breaks -> 3 segments)
// - Now on page 2 which is index 3 in the new sequence
// - The renderer handles this remapping automatically
//
// IMPORTANT: External code should NEVER call renderer methods and then try to
// "fix up" or "adjust" the result. Just call the method and trust it.

class PageRenderer {
public:
    PageRenderer(Document* document);

    // ==================== QUERY CURRENT STATE ====================
    
    // Get the total number of pages (virtual count in performance mode)
    // This is what controls should use for their maximum value
    int page_count() const;
    
    // Get current 0-based index into the page sequence
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
    
    // Go to a specific index in the sequence (0-based)
    // This is what controls call: scrollbar, combobox, etc.
    // Index must be in range [0, page_count()-1]
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

private:
    Document* document_;
    
    // The single source of truth: current position in the virtual page sequence
    int current_index_;
    
    // Map an index to physical page and segment
    struct Position {
        int physical_page;  // 1-based physical page number
        int segment_index;  // 0-based segment within the page
    };
    Position index_to_position(int index) const;
    
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
