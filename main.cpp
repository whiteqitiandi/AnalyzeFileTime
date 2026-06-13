#include "AnalyzeFileTime.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    AnalyzeFileTime window;
    window.show();
    return app.exec();
}
