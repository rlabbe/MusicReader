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





BOOL WINAPI ctrl_handler(DWORD /*ctrl_type*/)
{
    return FALSE; // Allow default behavior (process exits)
}


int main(int argc, char *argv[])
{
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


