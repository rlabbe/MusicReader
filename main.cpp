#include "musicreader.h"
#include <QtWidgets/QApplication>
#include "logger.h"
#include "bookmark_setter.h"


BOOL WINAPI ctrl_handler(DWORD /*ctrl_type*/)
{
    BookmarkSetter::shutdown();
    return FALSE; // Allow default behavior (process exits)
}


int main(int argc, char *argv[])
{
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    BookmarkSetter::startup();

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
    BookmarkSetter::shutdown();

    return result;
}


