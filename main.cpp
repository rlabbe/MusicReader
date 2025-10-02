#include "musicreader.h"
#include <QtWidgets/QApplication>
#include "logger.h"
#include <iostream>

#pragma warning(disable: 4611)
#pragma warning(push,1)
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#pragma warning(pop)

#include <vector>
#include <string>
#include <tuple>

#include "fitz_utils.h"
#include "imslp_client.h"

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
        for (const auto &pdf : pdfs) {
            std::cout << "File: " << pdf.filename << std::endl;
            std::cout << "PDF URL: " << pdf.url << std::endl;
            std::cout << "Thumbnail URL: " << pdf.thumb_url << std::endl;
            std::cout << "Thumbnail MIME: " << pdf.thumb_mime << std::endl;
            std::cout << "Size: " << pdf.size << " bytes" << std::endl;
            std::cout << "---" << std::endl;
        }

        // Set breakpoint here to inspect pdfs vector in debugger
        // Each pdf.thumb_url should contain a direct link to the first page image

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }


}

int main(int argc, char *argv[])
{
    //test_imslp_client();
    // test_strip_extra_call_info(); // in logger.h

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    int result = 0;
    {
        QApplication app(argc, argv);
        app.setStyle("fusion");

        MusicReader w;
        w.show();


        for (int i = 1; i < argc; ++i)
            w.open_pdf_in_tab(argv[i], 1);

        result = app.exec();
    }
    logger::shutdown();

    return result;
}
