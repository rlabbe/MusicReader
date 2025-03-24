#include "musicreader.h"
#include <QtWidgets/QApplication>
#include "logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    logger::initialize();

    app.setStyle("fusion");

    MusicReader w;
    w.show();

    for (int i = 1; i < argc; ++i)
        w.open_pdf_in_tab(argv[i], 1);
    
    int result = app.exec();
    logger::shutdown();
    return result;
}


