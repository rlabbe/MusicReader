#include "music_reader.h"
#include <QtWidgets/QApplication>
#include "logger.h"
#include <iostream>
#include "imslp_client.h"
#include "fitz_utils.h"
#include "font_info.h"
#include <filesystem>
#include <QImage>
#include <QPainter>

#pragma warning(push, 0)
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#pragma warning(pop)


BOOL WINAPI ctrl_handler(DWORD /*ctrl_type*/)
{
    return FALSE; // Allow default behavior (process exits)
}


void test_imslp_client()
{
    try {
        IMSLPClient client;

        auto pdfs = client.get_work_pdfs("BWV 934");

        std::cout << "\nFound " << pdfs.size() << " PDFs with thumbnails:" << std::endl;
        for (const auto& pdf : pdfs) {
            std::cout << "File: " << pdf.filename << std::endl;
            std::cout << "PDF URL: " << pdf.url << std::endl;
            std::cout << "Thumbnail URL: " << pdf.thumb_url << std::endl;
            std::cout << "Thumbnail MIME: " << pdf.thumb_mime << std::endl;
            std::cout << "Size: " << pdf.size << " bytes" << std::endl;
            std::cout << "---" << std::endl;
        }

        // Set breakpoint here to inspect pdfs vector in debugger
        // Each pdf.thumb_url should contain a direct link to the first page image

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void test_annotation_coordinates()
{
    // Test annotation writing to verify coordinate system
    // This writes text at known positions to help debug the coordinate/size mismatch

    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_annotations.pdf";

    // Create blank PDF if it doesn't exist
    if (!std::filesystem::exists(test_pdf)) {
        std::cout << "Creating blank test PDF: " << test_pdf.string() << std::endl;

        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
        if (!ctx) {
            std::cerr << "Failed to create context for test PDF" << std::endl;
            return;
        }

        pdf_document* pdf = pdf_create_document(ctx);
        if (!pdf) {
            std::cerr << "Failed to create PDF document" << std::endl;
            fz_drop_context(ctx);
            return;
        }

        // Create a blank US Letter page (612x792 points)
        fz_rect mediabox = {0, 0, 612, 792};
        pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);

        // Save the PDF
        pdf_write_options opts = pdf_default_write_options;
        pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);

        pdf_drop_document(ctx, pdf);
        fz_drop_context(ctx);

        std::cout << "Created blank test PDF successfully" << std::endl;
    }

    std::cout << "=== ANNOTATION COORDINATE TEST START ===" << std::endl;
    std::cout << "NOTE: Incremental saves may fail on freshly created PDFs." << std::endl;
    std::cout << "If tests fail, try running again (PDF will already exist for incremental save)." << std::endl;

    // Test 1: Top-left corner (should appear near top-left)
    // PDF coordinate system: (0,0) = bottom-left, so top-left of US Letter (612x792) is (0, 792)
    TextResult r1 = add_text_to_pdf(test_pdf, "TOP-LEFT", 1, 50.0f, 742.0f, 14.0f, "Consolas", 255, 0, 0);
    std::cout << "Test 1 (top-left): x=50, y=742 (50pt from top) - Result: " << static_cast<int>(r1);
    if (r1 != TextResult::Success)
        std::cout << " (FAILED - check if incremental save issue)";
    std::cout << std::endl;

    // Test 2: Middle of page
    TextResult r2 = add_text_to_pdf(test_pdf, "MIDDLE", 1, 306.0f, 396.0f, 14.0f, "Consolas", 0, 255, 0);
    std::cout << "Test 2 (middle): x=306, y=396 (center) - Result: " << static_cast<int>(r2) << std::endl;

    // Test 3: Bottom-left corner (should appear near bottom-left)
    TextResult r3 = add_text_to_pdf(test_pdf, "BOTTOM-LEFT", 1, 50.0f, 50.0f, 14.0f, "Consolas", 0, 0, 255);
    std::cout << "Test 3 (bottom-left): x=50, y=50 (50pt from bottom) - Result: " << static_cast<int>(r3) << std::endl;

    // Test 4: Large font to verify size handling
    TextResult r4 = add_text_to_pdf(test_pdf, "BIG TEXT", 1, 200.0f, 600.0f, 24.0f, "Consolas", 255, 0, 255);
    std::cout << "Test 4 (large font): x=200, y=600, font=24pt - Result: " << static_cast<int>(r4) << std::endl;

    std::cout << "=== ANNOTATION COORDINATE TEST END ===" << std::endl;
    std::cout << "Open test_annotations.pdf to verify text appears at expected positions" << std::endl;
}


void test_create_blank_pdf()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_from_bmp.pdf";
    std::cout << "=== STEP 1: CREATE BLANK PDF ===" << std::endl;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) {
        std::cerr << "Failed to create MuPDF context" << std::endl;
        return;
    }

    fz_try(ctx)
    {
        pdf_document* pdf = pdf_create_document(ctx);
        std::cout << "Created PDF document" << std::endl;

        // Create a blank US Letter page (612x792 points)
        fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
        pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);

        // Insert the page into the document at position 0
        pdf_insert_page(ctx, pdf, 0, page_obj);
        std::cout << "Added blank page" << std::endl;

        // Save the PDF
        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 0;
        pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
        std::cout << "Saved blank PDF to: " << test_pdf.string() << std::endl;

        pdf_drop_document(ctx, pdf);
    }
    fz_catch(ctx)
    {
        std::cerr << "Error: " << fz_caught_message(ctx) << std::endl;
    }

    fz_drop_context(ctx);

    std::cout << "=== VERIFY test_blank.pdf opens correctly, then move to step 2 ===" << std::endl;
}

void test_bmp_to_pdf_annotation()
{
    std::filesystem::path pdf_path = "D:/dev/MusicReader/test_from_bmp.pdf";

    std::cout << "=== CREATING BLANK PDF ===" << std::endl;

    // Parametrize size for easy switching between test mode and normal mode
    const int page_width = 800;
    const int page_height = 600;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) {
        std::cerr << "Failed to create context" << std::endl;
        return;
    }

    fz_try(ctx)
    {
        pdf_document* pdf = pdf_create_document(ctx);
        fz_rect mediabox = fz_make_rect(0, 0, page_width, page_height);
        pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(ctx, pdf, 0, page_obj);

        // Add "HERE" annotation - scale position based on page size
        pdf_obj* annots = pdf_new_array(ctx, pdf, 1);
        pdf_dict_put(ctx, page_obj, PDF_NAME(Annots), annots);

        pdf_obj* annot = pdf_new_dict(ctx, pdf, 10);
        pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

        // Scale annotation position from original 800x600
        float annot_x0 = 100.0f * page_width / 800.0f;
        float annot_y0 = 480.0f * page_height / 600.0f;
        float annot_x1 = 160.0f * page_width / 800.0f;
        float annot_y1 = 500.0f * page_height / 600.0f;

        pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
        pdf_array_push_real(ctx, rect, annot_x0);
        pdf_array_push_real(ctx, rect, annot_y0);
        pdf_array_push_real(ctx, rect, annot_x1);
        pdf_array_push_real(ctx, rect, annot_y1);
        pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);
        pdf_drop_obj(ctx, rect);

        pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), "HEREz");
        pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), "/Helv 14 Tf 1 0 0 rg");
        pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 0);

        pdf_array_push(ctx, annots, annot);
        pdf_drop_obj(ctx, annot);

        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 0;
        pdf_save_document(ctx, pdf, pdf_path.string().c_str(), &opts);
        pdf_drop_document(ctx, pdf);

        std::cout << "Saved blank PDF to " << pdf_path.string() << std::endl;
    }
    fz_catch(ctx)
    {
        std::cerr << "Error: " << fz_caught_message(ctx) << std::endl;
    }

    fz_drop_context(ctx);

    std::cout << "\n=== DONE ===" << std::endl;
}

void test_add_one_annotation()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_corners.pdf";
    std::cout << "=== STEP 2: CREATE PDF WITH CORNER ANNOTATIONS ===" << std::endl;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) {
        std::cerr << "Failed to create MuPDF context" << std::endl;
        return;
    }

    fz_try(ctx)
    {
        pdf_document* pdf = pdf_create_document(ctx);
        std::cout << "Created PDF document" << std::endl;

        // Create a blank US Letter page (612x792 points)
        fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
        pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(ctx, pdf, 0, page_obj);
        std::cout << "Added blank page (612x792 points)" << std::endl;

        // Get or create Annots array
        pdf_obj* annots = pdf_dict_get(ctx, page_obj, PDF_NAME(Annots));
        if (!annots) {
            annots = pdf_new_array(ctx, pdf, 4);
            pdf_dict_put(ctx, page_obj, PDF_NAME(Annots), annots);
        }

        // Annotation 1: TOP-LEFT corner (RED)
        // Position: 50pt from left, 50pt from top (y=792-50=742)
        {
            pdf_obj* annot = pdf_new_dict(ctx, pdf, 10);
            pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

            pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
            pdf_array_push_real(ctx, rect, 50.0f);  // x0
            pdf_array_push_real(ctx, rect, 742.0f); // y0
            pdf_array_push_real(ctx, rect, 150.0f); // x1
            pdf_array_push_real(ctx, rect, 760.0f); // y1
            pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);
            pdf_drop_obj(ctx, rect);

            pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), "TOP-LEFT");
            pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), "/Helv 14 Tf 1 0 0 rg");
            pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 0);

            pdf_array_push(ctx, annots, annot);
            pdf_drop_obj(ctx, annot);
            std::cout << "Added TOP-LEFT (red) at (50, 742)" << std::endl;
        }

        // Annotation 2: TOP-RIGHT corner (GREEN)
        // Position: 50pt from right, 50pt from top (x=612-150=462, y=742)
        {
            pdf_obj* annot = pdf_new_dict(ctx, pdf, 10);
            pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

            pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
            pdf_array_push_real(ctx, rect, 462.0f); // x0
            pdf_array_push_real(ctx, rect, 742.0f); // y0
            pdf_array_push_real(ctx, rect, 562.0f); // x1 (50pt from right edge)
            pdf_array_push_real(ctx, rect, 760.0f); // y1
            pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);
            pdf_drop_obj(ctx, rect);

            pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), "TOP-RIGHT");
            pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), "/Helv 14 Tf 0 1 0 rg");
            pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 0);

            pdf_array_push(ctx, annots, annot);
            pdf_drop_obj(ctx, annot);
            std::cout << "Added TOP-RIGHT (green) at (462, 742)" << std::endl;
        }

        // Annotation 3: BOTTOM-LEFT corner (BLUE)
        // Position: 50pt from left, 50pt from bottom (y=50)
        {
            pdf_obj* annot = pdf_new_dict(ctx, pdf, 10);
            pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

            pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
            pdf_array_push_real(ctx, rect, 50.0f);  // x0
            pdf_array_push_real(ctx, rect, 32.0f);  // y0 (50pt from bottom = 50, minus 18pt height = 32)
            pdf_array_push_real(ctx, rect, 180.0f); // x1
            pdf_array_push_real(ctx, rect, 50.0f);  // y1
            pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);
            pdf_drop_obj(ctx, rect);

            pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), "BOTTOM-LEFT");
            pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), "/Helv 14 Tf 0 0 1 rg");
            pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 0);

            pdf_array_push(ctx, annots, annot);
            pdf_drop_obj(ctx, annot);
            std::cout << "Added BOTTOM-LEFT (blue) at (50, 32-50)" << std::endl;
        }

        // Annotation 4: BOTTOM-RIGHT corner (MAGENTA)
        // Position: 50pt from right, 50pt from bottom
        {
            pdf_obj* annot = pdf_new_dict(ctx, pdf, 10);
            pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

            pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
            pdf_array_push_real(ctx, rect, 442.0f); // x0
            pdf_array_push_real(ctx, rect, 32.0f);  // y0
            pdf_array_push_real(ctx, rect, 562.0f); // x1
            pdf_array_push_real(ctx, rect, 50.0f);  // y1
            pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);
            pdf_drop_obj(ctx, rect);

            pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), "BOTTOM-RIGHT");
            pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), "/Helv 14 Tf 1 0 1 rg");
            pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 0);

            pdf_array_push(ctx, annots, annot);
            pdf_drop_obj(ctx, annot);
            std::cout << "Added BOTTOM-RIGHT (magenta) at (442, 32-50)" << std::endl;
        }

        // Annotation 5: CENTER (YELLOW)
        // Position: center of page (306, 396)
        {
            pdf_obj* annot = pdf_new_dict(ctx, pdf, 10);
            pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(FreeText));

            pdf_obj* rect = pdf_new_array(ctx, pdf, 4);
            pdf_array_push_real(ctx, rect, 276.0f); // x0 (center - 30)
            pdf_array_push_real(ctx, rect, 387.0f); // y0 (center - 9)
            pdf_array_push_real(ctx, rect, 336.0f); // x1 (center + 30)
            pdf_array_push_real(ctx, rect, 405.0f); // y1 (center + 9)
            pdf_dict_put(ctx, annot, PDF_NAME(Rect), rect);
            pdf_drop_obj(ctx, rect);

            pdf_dict_put_text_string(ctx, annot, PDF_NAME(Contents), "CENTER");
            pdf_dict_put_text_string(ctx, annot, PDF_NAME(DA), "/Helv 14 Tf 1 1 0 rg");
            pdf_dict_put_int(ctx, annot, PDF_NAME(Q), 1); // Center aligned

            pdf_array_push(ctx, annots, annot);
            pdf_drop_obj(ctx, annot);
            std::cout << "Added CENTER (yellow) at (276-336, 387-405)" << std::endl;
        }

        // Save the PDF
        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 0;
        pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
        std::cout << "Saved PDF to: " << test_pdf.string() << std::endl;

        pdf_drop_document(ctx, pdf);
    }
    fz_catch(ctx)
    {
        std::cerr << "Error: " << fz_caught_message(ctx) << std::endl;
    }

    fz_drop_context(ctx);

    std::cout << "\n=== VERIFY test_corners.pdf ===" << std::endl;
    std::cout << "Should show labels at all 4 corners + center:" << std::endl;
    std::cout << "  TOP-LEFT (red) - top left corner" << std::endl;
    std::cout << "  TOP-RIGHT (green) - top right corner" << std::endl;
    std::cout << "  BOTTOM-LEFT (blue) - bottom left corner" << std::endl;
    std::cout << "  BOTTOM-RIGHT (magenta) - bottom right corner" << std::endl;
    std::cout << "  CENTER (yellow) - center of page" << std::endl;
}

void create_test_pdf_method1_simple_text()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_method1_simple.pdf";
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return;

    fz_try(ctx)
    {
        pdf_document* pdf = pdf_create_document(ctx);
        fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
        pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(ctx, pdf, 0, page_obj);

        // Method 1: Simple positioned text
        pdf_obj* resources = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
        if (!resources) {
            resources = pdf_new_dict(ctx, pdf, 2);
            pdf_dict_put(ctx, page_obj, PDF_NAME(Resources), resources);
            pdf_drop_obj(ctx, resources);
            resources = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
        }

        pdf_obj* fonts = pdf_dict_get(ctx, resources, PDF_NAME(Font));
        if (!fonts) {
            fonts = pdf_new_dict(ctx, pdf, 1);
            pdf_dict_put(ctx, resources, PDF_NAME(Font), fonts);
            pdf_drop_obj(ctx, fonts);
            fonts = pdf_dict_get(ctx, resources, PDF_NAME(Font));
        }

        pdf_obj* font_dict = pdf_new_dict(ctx, pdf, 3);
        pdf_dict_put(ctx, font_dict, PDF_NAME(Type), PDF_NAME(Font));
        pdf_dict_put(ctx, font_dict, PDF_NAME(Subtype), PDF_NAME(Type1));
        pdf_dict_put_name(ctx, font_dict, PDF_NAME(BaseFont), "Helvetica");
        pdf_dict_puts(ctx, fonts, "F1", font_dict);
        pdf_drop_obj(ctx, font_dict);

        fz_buffer* buf = fz_new_buffer(ctx, 1024);
        fz_append_string(ctx, buf, "BT\n");
        fz_append_string(ctx, buf, "/F1 24 Tf\n");
        fz_append_string(ctx, buf, "100 700 Td\n");
        fz_append_string(ctx, buf, "(hi) Tj\n");
        fz_append_string(ctx, buf, "ET\n");

        pdf_obj* contents = pdf_add_stream(ctx, pdf, buf, nullptr, 0);
        pdf_dict_put(ctx, page_obj, PDF_NAME(Contents), contents);
        pdf_drop_obj(ctx, contents);
        fz_drop_buffer(ctx, buf);

        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 0;
        pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
        pdf_drop_document(ctx, pdf);
        std::cout << "Created: " << test_pdf.string() << std::endl;
    }
    fz_catch(ctx)
    {
        std::cerr << "Error: " << fz_caught_message(ctx) << std::endl;
    }
    fz_drop_context(ctx);
}

void create_test_pdf_method2_textbox()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_method2_box.pdf";
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return;

    fz_try(ctx)
    {
        pdf_document* pdf = pdf_create_document(ctx);
        fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
        pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(ctx, pdf, 0, page_obj);

        // Method 2: Text with clipping box
        pdf_obj* resources = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
        if (!resources) {
            resources = pdf_new_dict(ctx, pdf, 2);
            pdf_dict_put(ctx, page_obj, PDF_NAME(Resources), resources);
            pdf_drop_obj(ctx, resources);
            resources = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
        }

        pdf_obj* fonts = pdf_dict_get(ctx, resources, PDF_NAME(Font));
        if (!fonts) {
            fonts = pdf_new_dict(ctx, pdf, 1);
            pdf_dict_put(ctx, resources, PDF_NAME(Font), fonts);
            pdf_drop_obj(ctx, fonts);
            fonts = pdf_dict_get(ctx, resources, PDF_NAME(Font));
        }

        pdf_obj* font_dict = pdf_new_dict(ctx, pdf, 3);
        pdf_dict_put(ctx, font_dict, PDF_NAME(Type), PDF_NAME(Font));
        pdf_dict_put(ctx, font_dict, PDF_NAME(Subtype), PDF_NAME(Type1));
        pdf_dict_put_name(ctx, font_dict, PDF_NAME(BaseFont), "Helvetica");
        pdf_dict_puts(ctx, fonts, "F1", font_dict);
        pdf_drop_obj(ctx, font_dict);

        fz_buffer* buf = fz_new_buffer(ctx, 1024);
        fz_append_string(ctx, buf, "q\n");                     // Save state
        fz_append_string(ctx, buf, "100 680 300 40 re W n\n"); // Clip to box
        fz_append_string(ctx, buf, "BT\n");
        fz_append_string(ctx, buf, "/F1 24 Tf\n");
        fz_append_string(ctx, buf, "100 700 Td\n");
        fz_append_string(ctx, buf, "(hi) Tj\n");
        fz_append_string(ctx, buf, "ET\n");
        fz_append_string(ctx, buf, "Q\n"); // Restore state

        pdf_obj* contents = pdf_add_stream(ctx, pdf, buf, nullptr, 0);
        pdf_dict_put(ctx, page_obj, PDF_NAME(Contents), contents);
        pdf_drop_obj(ctx, contents);
        fz_drop_buffer(ctx, buf);

        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 0;
        pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
        pdf_drop_document(ctx, pdf);
        std::cout << "Created: " << test_pdf.string() << std::endl;
    }
    fz_catch(ctx)
    {
        std::cerr << "Error: " << fz_caught_message(ctx) << std::endl;
    }
    fz_drop_context(ctx);
}

void create_test_pdf_method3_xobject()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_method3_xobject.pdf";
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return;

    fz_try(ctx)
    {
        pdf_document* pdf = pdf_create_document(ctx);
        fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
        pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(ctx, pdf, 0, page_obj);

        // Method 3: Text as XObject (form object)
        pdf_obj* resources = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
        if (!resources) {
            resources = pdf_new_dict(ctx, pdf, 2);
            pdf_dict_put(ctx, page_obj, PDF_NAME(Resources), resources);
            pdf_drop_obj(ctx, resources);
            resources = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
        }

        // Create font for XObject
        pdf_obj* xobj_resources = pdf_new_dict(ctx, pdf, 1);
        pdf_obj* xobj_fonts = pdf_new_dict(ctx, pdf, 1);
        pdf_obj* font_dict = pdf_new_dict(ctx, pdf, 3);
        pdf_dict_put(ctx, font_dict, PDF_NAME(Type), PDF_NAME(Font));
        pdf_dict_put(ctx, font_dict, PDF_NAME(Subtype), PDF_NAME(Type1));
        pdf_dict_put_name(ctx, font_dict, PDF_NAME(BaseFont), "Helvetica");
        pdf_dict_puts(ctx, xobj_fonts, "F1", font_dict);
        pdf_drop_obj(ctx, font_dict);
        pdf_dict_put(ctx, xobj_resources, PDF_NAME(Font), xobj_fonts);
        pdf_drop_obj(ctx, xobj_fonts);

        // Create XObject stream
        fz_buffer* xobj_buf = fz_new_buffer(ctx, 256);
        fz_append_string(ctx, xobj_buf, "BT\n");
        fz_append_string(ctx, xobj_buf, "/F1 24 Tf\n");
        fz_append_string(ctx, xobj_buf, "0 0 Td\n");
        fz_append_string(ctx, xobj_buf, "(hi) Tj\n");
        fz_append_string(ctx, xobj_buf, "ET\n");

        pdf_obj* xobj = pdf_new_dict(ctx, pdf, 5);
        pdf_dict_put(ctx, xobj, PDF_NAME(Type), PDF_NAME(XObject));
        pdf_dict_put(ctx, xobj, PDF_NAME(Subtype), PDF_NAME(Form));
        pdf_obj* bbox = pdf_new_array(ctx, pdf, 4);
        pdf_array_push_real(ctx, bbox, 0);
        pdf_array_push_real(ctx, bbox, 0);
        pdf_array_push_real(ctx, bbox, 100);
        pdf_array_push_real(ctx, bbox, 30);
        pdf_dict_put(ctx, xobj, PDF_NAME(BBox), bbox);
        pdf_drop_obj(ctx, bbox);
        pdf_dict_put(ctx, xobj, PDF_NAME(Resources), xobj_resources);
        pdf_drop_obj(ctx, xobj_resources);

        pdf_obj* xobj_ref = pdf_add_stream(ctx, pdf, xobj_buf, xobj, 0);
        fz_drop_buffer(ctx, xobj_buf);
        pdf_drop_obj(ctx, xobj);

        // Add XObject to page resources
        pdf_obj* xobjects = pdf_new_dict(ctx, pdf, 1);
        pdf_dict_puts(ctx, xobjects, "X1", xobj_ref);
        pdf_dict_put(ctx, resources, PDF_NAME(XObject), xobjects);
        pdf_drop_obj(ctx, xobjects);
        pdf_drop_obj(ctx, xobj_ref);

        // Use XObject in page content
        fz_buffer* buf = fz_new_buffer(ctx, 256);
        fz_append_string(ctx, buf, "q\n");
        fz_append_string(ctx, buf, "1 0 0 1 100 700 cm\n"); // Position
        fz_append_string(ctx, buf, "/X1 Do\n");             // Draw XObject
        fz_append_string(ctx, buf, "Q\n");

        pdf_obj* contents = pdf_add_stream(ctx, pdf, buf, nullptr, 0);
        pdf_dict_put(ctx, page_obj, PDF_NAME(Contents), contents);
        pdf_drop_obj(ctx, contents);
        fz_drop_buffer(ctx, buf);

        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 0;
        pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
        pdf_drop_document(ctx, pdf);
        std::cout << "Created: " << test_pdf.string() << std::endl;
    }
    fz_catch(ctx)
    {
        std::cerr << "Error: " << fz_caught_message(ctx) << std::endl;
    }
    fz_drop_context(ctx);
}

void test_marked_content()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_marked_content.pdf";

    // Create blank PDF
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return;

    pdf_document* pdf = pdf_create_document(ctx);
    fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
    pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
    pdf_insert_page(ctx, pdf, 0, page_obj);

    pdf_write_options opts = pdf_default_write_options;
    opts.do_incremental = 0;
    pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
    pdf_drop_document(ctx, pdf);
    fz_drop_context(ctx);

    // Add marked text using Georgia font
    std::filesystem::path georgia_font = "C:/Windows/Fonts/georgia.ttf";
    TextResult result = add_marked_text_to_pdf(test_pdf, "Hello Georgia!", 1, 100.0f, 700.0f, 24.0f, "Georgia",
                                               georgia_font, 0, 0, 255);

    if (result == TextResult::Success) {
        std::cout << "Created: " << test_pdf.string() << " with marked content" << std::endl;
    } else {
        std::cerr << "Failed to add marked text, error code: " << static_cast<int>(result) << std::endl;
    }
}

void test_bravura_freetext()
{
    std::filesystem::path test_pdf = "D:/dev/MusicReader/test_bravura_annot.pdf";

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx)
        return;

    pdf_document* pdf = pdf_create_document(ctx);
    fz_rect mediabox = fz_make_rect(0, 0, 612, 792);
    pdf_obj* page_obj = pdf_add_page(ctx, pdf, mediabox, 0, nullptr, nullptr);
    pdf_insert_page(ctx, pdf, 0, page_obj);

    pdf_write_options opts = pdf_default_write_options;
    opts.do_incremental = 0;
    pdf_save_document(ctx, pdf, test_pdf.string().c_str(), &opts);
    pdf_drop_document(ctx, pdf);
    fz_drop_context(ctx);

    // Bravura SMuFL: U+E262 sharp, U+E260 flat, U+E261 natural — UTF-8 encoded.
    std::string text = "\xEE\x89\xA2 \xEE\x89\xA0 \xEE\x89\xA1";

    auto bravura = lookup_font_file("Bravura");
    if (!bravura) {
        std::cerr << "Bravura font not found via Windows registry. Is it installed?" << std::endl;
        return;
    }

    TextResult result = add_freetext_with_custom_font(test_pdf, text, 1,
                                                     100.0f, 650.0f, 200.0f, 60.0f,
                                                     48.0f, "Bravura", *bravura,
                                                     0, 0, 0);

    if (result == TextResult::Success)
        std::cout << "Created: " << test_pdf.string() << " with Bravura FreeText annotation" << std::endl;
    else
        std::cerr << "Failed to add Bravura annotation, error code: " << static_cast<int>(result) << std::endl;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    // test_marked_content();
    // test_create_blank_pdf();
    //test_bravura_freetext();

    int result = 0;
    {
        QApplication app(argc, argv);
        app.setStyle("fusion");
        QStyleHints* hints = QGuiApplication::styleHints();
        hints->setColorScheme(Qt::ColorScheme::Dark);

        MusicReader w;
        w.show();

        for (int i = 1; i < argc; ++i)
            w.open_pdf_in_tab(argv[i], 1);

        result = app.exec();
    }
    logger::shutdown();

    return result;
}
