#include <QApplication>
#include "mainwindow.h" 
#include "findex.hpp"

int main(int argc, char *argv[])
{
    Findex findex;
    findex.elevate();
    findex.run('C');

    QApplication app(argc, argv);

    MainWindow w(findex.indexer);
    w.show();

    return app.exec();
}
