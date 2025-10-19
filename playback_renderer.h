#pragma once

#include <memory>
#include <string>
#include "page.h"
#include "playback_mode.h"

class Document;

// Abstracts playback navigation and page rendering.
// Queries global PlaybackMode to determine behavior.
//
// External code (PDFViewer) only needs to call:
//   - get_current_page() to display
//   - next() / prev() to navigate
//   - current_page_display() to show page number
//   - goto_page(physical) to jump to a specific page
//
// The renderer internally manages which physical page to show,
// whether to crop it, and how to format the display string.
class PlaybackRenderer {
public:
    PlaybackRenderer(std::shared_ptr<Document> document);

    // Get the current page to display (may be cropped in performance mode)
    Page get_current_page() const;

    // Navigate forward/backward in the playback sequence
    void next();
    void prev();

    // Jump to a specific physical page number (from bookmark or goto dialog)
    void goto_page(int physical_page);

    // Get display string for current position (e.g., "1a", "2", "3b")
    std::string current_page_display() const;

    // Get total physical page count
    int total_pages() const;

    // Check if can navigate in either direction
    bool can_go_next() const;
    bool can_go_prev() const;

private:
    std::shared_ptr<Document> document_;

    // Current state
    int current_physical_page_;
    int current_segment_index_;  // Which break segment on the current page (0 = first)

    // Helper methods for performance mode
    int segment_count(int physical_page) const;
    Page crop_page(const Page& page, int physical_page, int segment_index) const;
    std::string format_page_display(int physical_page, int segment_index) const;
};
