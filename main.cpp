#include "musicreader.h"
#include <QtWidgets/QApplication>

#include "config_file.h"
#include "logger.h"
#include "config_dialog.h"
#pragma warning(push, 0)
#include <mupdf/fitz.h>
#pragma warning(pop)
#include "document.h"

std::string input("C:/smusic/MusicReader/China-Gates.pdf");


void m3()
{
    auto doc = Document(input, 100);
    [[maybe_unused]] auto page = doc.get_page(0);
}

int m2()
{

    float zoom, rotate;
    int page_number, page_count;
    fz_context *ctx;
    fz_document *doc;
    fz_pixmap *pix;
    fz_matrix ctm;
    int x, y;


    page_number = 1;
    zoom = 100.;
    rotate = 0;

    /* Create a context to hold the exception stack and various caches. */
    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "cannot create mupdf context\n");
        return EXIT_FAILURE;
    }

    /* Register the default file types to handle. */
    fz_try(ctx)
        fz_register_document_handlers(ctx);
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        fprintf(stderr, "cannot register document handlers\n");
        fz_drop_context(ctx);
        return EXIT_FAILURE;
    }

    /* Open the document. */
    fz_try(ctx)
        doc = fz_open_document(ctx, input.c_str());
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        fprintf(stderr, "cannot open document\n");
        fz_drop_context(ctx);
        return EXIT_FAILURE;
    }

    /* Count the number of pages. */
    fz_try(ctx)
        page_count = fz_count_pages(ctx, doc);
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        fprintf(stderr, "cannot count number of pages\n");
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return EXIT_FAILURE;
    }

    if (page_number < 0 || page_number >= page_count)
    {
        fprintf(stderr, "page number out of range: %d (page count %d)\n", page_number + 1, page_count);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return EXIT_FAILURE;
    }

    /* Compute a transformation matrix for the zoom and rotation desired. */
    /* The default resolution without scaling is 72 dpi. */
    ctm = fz_scale(zoom / 100, zoom / 100);
    ctm = fz_pre_rotate(ctm, rotate);

    /* Render page to an RGB pixmap. */
    fz_try(ctx)
        pix = fz_new_pixmap_from_page_number(ctx, doc, page_number, ctm, fz_device_rgb(ctx), 0);
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        fprintf(stderr, "cannot render page\n");
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return EXIT_FAILURE;
    }

    /* Print image data in ascii PPM format. */
    printf("P3\n");
    printf("%d %d\n", pix->w, pix->h);
    printf("255\n");
    for (y = 0; y < pix->h; ++y)
    {
        unsigned char *p = &pix->samples[y * pix->stride];
        for (x = 0; x < pix->w; ++x)
        {
            if (x > 0)
                printf("  ");
            printf("%3d %3d %3d", p[0], p[1], p[2]);
            p += pix->n;
        }
        printf("\n");
    }

    /* Clean up. */
    fz_drop_pixmap(ctx, pix);
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    return EXIT_SUCCESS;
}



int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    //m2();
    //m3();
    //return 0;
    logger::initialize("./mr.log");

    app.setStyle("fusion");

    MusicReader w;
    w.show();
    return app.exec();
}


