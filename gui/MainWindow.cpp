#include "MainWindow.h"
#include "indexer.hpp"
#include <QLabel>
#include <QString>

MainWindow::MainWindow(USNIndexer& indexer, QWidget* parent) : QMainWindow(parent), indexer(indexer)
{
    file_name_label = new QLabel(this);
    file_name_label->setGeometry(10, 10, 200, 30);
}
void MainWindow::updateLabels() {
    QString qstr_file_name = QString::fromStdWString(indexer.file_name_wstring);
    file_name_label->setText(qstr_file_name);
}
MainWindow::~MainWindow() {}
