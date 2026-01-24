#pragma once

#include <QPoint>
#include <QFontMetrics>
#include "font_info.h"

// ============================================================================
// Annotation Coordinate System
// ============================================================================
//
// Centralizes coordinate transformations for annotations across three systems:
//
// 1. PDF Coordinates (MuPDF storage)
//    - Origin at bottom-left of page, Y increases upward
//    - Units: points (1/72 inch)
//
// 2. Screen Coordinates (MuPDF API for rect operations)
//    - Origin at top-left of page, Y increases downward
//    - Units: points (1/72 inch)
//
// 3. Display Coordinates (Qt widgets)
//    - Origin at top-left of displayed image, Y increases downward
//    - Units: pixels (scaled by DPI)
//
// Note: pdf_set_annot_rect() expects screen coordinates, but the /Rect
// stored in PDF uses PDF coordinates.
//
// MuPDF places text baseline at font_size below rect top.
// Qt places text baseline at ascent below the widget's content area top.
//
// ============================================================================

/// Context for a specific page - set once, use for all annotations on that page
struct PageContext {
    float page_height_points = 0.0f;    // PDF page height in points
    float page_width_points = 0.0f;     // PDF page width in points
    float display_width_pixels = 0.0f;  // Displayed image width in pixels
    float display_height_pixels = 0.0f; // Displayed image height in pixels

    /// Scale factor: display_pixels / pdf_points
    /// Multiply PDF points by this to get display pixels
    float points_to_pixels() const { return (page_width_points > 0) ? display_width_pixels / page_width_points : 1.0f; }

    /// Scale factor: pdf_points / display_pixels
    /// Multiply display pixels by this to get PDF points
    float pixels_to_points() const
    {
        return (display_width_pixels > 0) ? page_width_points / display_width_pixels : 1.0f;
    }

    bool is_valid() const
    {
        return page_height_points > 0 && page_width_points > 0 && display_width_pixels > 0 && display_height_pixels > 0;
    }
};


/// All the Y coordinates needed for one annotation, computed together
struct AnnotationYCoords {
    float baseline_pdf;    // Baseline Y in PDF coords - stored in Annotation.y_
    float rect_top_screen; // Rect top for pdf_set_annot_rect() - screen coords
    int editor_widget_y;   // Y position for Qt editor widget - display pixels
};


/// Centralizes all annotation coordinate math
class AnnotationCoordinates {
public:
    AnnotationCoordinates() = default;
    explicit AnnotationCoordinates(const PageContext& ctx)
        : ctx_(ctx)
    {
    }

    void set_context(const PageContext& ctx) { ctx_ = ctx; }
    const PageContext& context() const { return ctx_; }

    // ========================================================================
    // Coordinate System Conversions (Y-axis only, X is unchanged)
    // ========================================================================

    /// PDF Y (origin bottom) to Screen Y (origin top)
    float pdf_y_to_screen_y(float pdf_y) const { return ctx_.page_height_points - pdf_y; }

    /// Screen Y (origin top) to PDF Y (origin bottom)
    float screen_y_to_pdf_y(float screen_y) const { return ctx_.page_height_points - screen_y; }

    /// PDF points to display pixels
    float points_to_display(float points) const { return points * ctx_.points_to_pixels(); }

    /// Display pixels to PDF points
    float display_to_points(float pixels) const { return pixels * ctx_.pixels_to_points(); }

    // ========================================================================
    // Click Position -> Annotation Coordinates
    // ========================================================================

    /// Convert a display click position to PDF baseline coordinates.
    /// Call this when user clicks to create a new annotation.
    ///
    /// @param click_display_x  Click X in display pixels (relative to displayed image)
    /// @param click_display_y  Click Y in display pixels (relative to displayed image)
    /// @return                 {x, y} in PDF coordinates where y is the baseline
    std::pair<float, float> click_to_pdf_baseline(float click_display_x, float click_display_y) const
    {
        float x_points = display_to_points(click_display_x);
        float y_screen_points = display_to_points(click_display_y);
        float y_pdf_points = screen_y_to_pdf_y(y_screen_points);
        return {x_points, y_pdf_points};
    }

    // ========================================================================
    // Baseline (from Annotation) -> MuPDF Rect Top
    // ========================================================================

    /// Given a baseline Y in PDF coords, compute rect top Y in screen coords.
    /// Use this when calling pdf_set_annot_rect() for save or preview.
    ///
    /// MuPDF positions text baseline at font_size below rect top.
    ///
    /// @param baseline_pdf_y  Baseline Y in PDF coords (Annotation.y_)
    /// @param font_size       Font size in points
    /// @return                Rect top Y in screen coords for MuPDF API
    float baseline_to_rect_top_screen(float baseline_pdf_y, float font_size) const
    {
        float baseline_screen_y = pdf_y_to_screen_y(baseline_pdf_y);
        return baseline_screen_y - font_size;
    }

    // ========================================================================
    // MuPDF Rect Top (from loaded PDF) -> Baseline
    // ========================================================================

    /// Given a rect top Y in PDF coords (from /Rect), compute baseline Y in PDF coords.
    /// Use this when loading annotations from PDF.
    ///
    /// @param rect_top_pdf_y  Rect top Y in PDF coords (y1 from /Rect array)
    /// @param font_size       Font size in points
    /// @return                Baseline Y in PDF coords for Annotation.y_
    float rect_top_to_baseline_pdf(float rect_top_pdf_y, float font_size) const { return rect_top_pdf_y - font_size; }

    // ========================================================================
    // Baseline (from Annotation) -> Qt Editor Widget Position
    // ========================================================================

    /// Given a baseline Y in PDF coords, compute Qt editor widget Y position.
    /// Use this when positioning the InPlaceAnnotationEditor.
    ///
    /// Qt's QTextEdit places text baseline at: widget_top + doc_margin + ascent
    /// So: widget_top = baseline_display - doc_margin - ascent
    ///
    /// @param baseline_pdf_y  Baseline Y in PDF coords (Annotation.y_)
    /// @param doc_margin      QTextEdit document margin in pixels
    /// @param ascent          Font ascent in pixels (from QFontMetrics)
    /// @return                Widget Y position in display pixels
    int baseline_to_editor_widget_y(float baseline_pdf_y, int doc_margin, int ascent) const
    {
        float baseline_screen_points = pdf_y_to_screen_y(baseline_pdf_y);
        float baseline_display_pixels = points_to_display(baseline_screen_points);
        return static_cast<int>(baseline_display_pixels) - doc_margin - ascent;
    }

    /// Convenience overload that takes display coordinates directly.
    /// Use when you already have the click position in display pixels.
    ///
    /// @param baseline_display_y  Baseline Y in display pixels (click Y)
    /// @param doc_margin          QTextEdit document margin in pixels
    /// @param ascent              Font ascent in pixels (from QFontMetrics)
    /// @return                    Widget Y position in display pixels
    static int baseline_display_to_editor_widget_y(int baseline_display_y, int doc_margin, int ascent)
    {
        return baseline_display_y - doc_margin - ascent;
    }

    // ========================================================================
    // Complete Coordinate Set
    // ========================================================================

    /// Compute all Y coordinates needed for an annotation from a click position.
    /// This is the main entry point when creating a new annotation.
    ///
    /// @param click_display_y  Click Y in display pixels
    /// @param font_size        Font size in PDF points
    /// @param doc_margin       QTextEdit document margin in pixels
    /// @param ascent           Font ascent in display pixels
    /// @return                 All Y coordinates needed
    AnnotationYCoords from_click(int click_display_y, float font_size, int doc_margin, int ascent) const
    {
        // Click position in display pixels IS the baseline in display coords
        float baseline_screen_points = display_to_points(static_cast<float>(click_display_y));
        float baseline_pdf = screen_y_to_pdf_y(baseline_screen_points);

        float rect_top_screen = baseline_to_rect_top_screen(baseline_pdf, font_size);

        int editor_y = baseline_display_to_editor_widget_y(click_display_y, doc_margin, ascent);

        return {baseline_pdf, rect_top_screen, editor_y};
    }

    /// Compute all Y coordinates needed for an annotation from a loaded baseline.
    /// This is the main entry point when displaying an existing annotation.
    ///
    /// @param baseline_pdf     Baseline Y in PDF coords (from Annotation.y_)
    /// @param font_size        Font size in PDF points
    /// @param doc_margin       QTextEdit document margin in pixels
    /// @param ascent           Font ascent in display pixels
    /// @return                 All Y coordinates needed
    AnnotationYCoords from_baseline_pdf(float baseline_pdf, float font_size, int doc_margin, int ascent) const
    {
        float rect_top_screen = baseline_to_rect_top_screen(baseline_pdf, font_size);

        float baseline_screen_points = pdf_y_to_screen_y(baseline_pdf);
        int baseline_display = static_cast<int>(points_to_display(baseline_screen_points));
        int editor_y = baseline_display_to_editor_widget_y(baseline_display, doc_margin, ascent);

        return {baseline_pdf, rect_top_screen, editor_y};
    }

private:
    PageContext ctx_;
};
