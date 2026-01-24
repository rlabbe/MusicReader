# Annotation System Design

## Overview

MusicReader supports adding text annotations to PDF pages. Annotations are stored in-memory during editing and persisted to the PDF as FreeText annotations when the document is saved.

## Architecture

### Data Flow

1. **User clicks** in annotation mode on the page
2. **InPlaceAnnotationEditor** (Qt QTextEdit) appears at click position for text input
3. **Live preview** renders via MuPDF temporary annotation on each keystroke
4. **On Enter**, annotation is stored in `Document::annotations_` (in-memory)
5. **On document save**, annotations are written to PDF via MuPDF FreeText annotation API

### Key Components

- **PDFViewer** (`pdf_viewer.cpp`) - Handles mouse clicks, cursor adjustment, and coordinates the editing flow
- **InPlaceAnnotationEditor** (`in_place_annotation_editor.cpp`) - Qt widget for text input, positioned to align baseline with click point
- **Document** (`document.cpp`) - Stores annotations in memory, handles save/load to PDF
- **AnnotationCoordinates** (`annotation_coords.h`) - Centralizes coordinate system conversions

## Coordinate Systems

Three coordinate systems are involved:

| System | Origin | Y Direction | Units |
|--------|--------|-------------|-------|
| PDF | Bottom-left | Up | Points (1/72 inch) |
| Screen (MuPDF API) | Top-left | Down | Points |
| Display (Qt) | Top-left | Down | Pixels |

### Key Relationships

- `pdf_set_annot_rect()` expects **screen coordinates**, but `/Rect` in PDF stores **PDF coordinates**
- MuPDF places text baseline at `font_size` below rect top
- Qt places text baseline at `ascent` below widget content area top

### Conversion Functions

All coordinate math is centralized in `AnnotationCoordinates`:

```cpp
// PDF <-> Screen
float pdf_y_to_screen_y(float pdf_y);
float screen_y_to_pdf_y(float screen_y);

// Points <-> Pixels
float points_to_display(float points);
float display_to_points(float pixels);

// Baseline <-> Rect conversions
float baseline_to_rect_top_screen(float baseline_pdf_y, float font_size);
float rect_top_to_baseline_pdf(float rect_top_pdf_y, float font_size);
```

## Cursor Handling

The IBeam cursor's hotspot is at its center, but users naturally align the bottom of the cursor with the target line. On Windows, the code uses `GetIconInfo()` to get the cursor metrics and adjusts the click position so the text baseline appears where the bottom of the IBeam was positioned.

## Preview Rendering

During text input, each keystroke triggers a preview update:

1. Create temporary FreeText annotation in MuPDF (not saved to PDF)
2. Re-render entire page at current DPI
3. Display rendered image
4. Delete temporary annotation

This is the "fast" approach. A potential optimization would render just the text as a small pixmap and composite it onto the existing page image.

## Persistence

### Save Flow

1. For each annotation in `Document::annotations_`:
   - Convert baseline Y to rect top (screen coords) using `baseline_to_rect_top_screen()`
   - Create FreeText annotation via `pdf_create_annot()`
   - Set rect, contents, default appearance, border, quadding
   - Call `pdf_update_annot()` to generate appearance stream

### Load Flow

1. Iterate all FreeText annotations in PDF
2. Extract rect, contents, font info from `/DA` string
3. Convert rect top Y to baseline using `rect_top_to_baseline_pdf()`
4. Store in `Document::annotations_`

## Font Handling

- Annotations use a configurable font (default: Helvetica 10pt black)
- Font family is a PDF Base-14 name, mapped to Qt system fonts for display
- MuPDF uses its own font name mapping for appearance streams
