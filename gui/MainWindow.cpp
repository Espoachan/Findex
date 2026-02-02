#include "mainwindow.h"
#include "indexer.hpp"
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(USNIndexer& indexer, QWidget* parent) : QMainWindow(parent), indexer(indexer) {
    this->resize(800, 600);
    this->showMaximized();

    QLabel *label = new QLabel("Yahallo!");

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(label);
    this->setLayout(layout);
}
MainWindow::~MainWindow() {}
