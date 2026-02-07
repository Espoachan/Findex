#include "mainwindow.h"
#include "indexer.hpp"
#include "Worker.h"

#include <QThread>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <QTimer>
#include <mutex>

MainWindow::MainWindow(USNIndexer& indexer_ref, QWidget* parent) : QMainWindow(parent), indexer(indexer_ref) 
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *layout = new QVBoxLayout(centralWidget);

    QLabel *statusLabel = new QLabel("Starting Indexer...");
    layout->addWidget(statusLabel);
    
    QLabel *countLabel = new QLabel("Files Indexed: 0");
    layout->addWidget(countLabel);

    setWindowTitle("Findex");
    resize(800, 600);
    showMaximized();

    setupWorker();

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, statusLabel, countLabel]() {
        std::lock_guard<std::mutex> lock(indexer.dataMutex);
        
        size_t count = indexer.index_map.size();
        countLabel->setText("Files Indexed: " + QString::number(count));
        
        if (count > 0) statusLabel->setText("Indexing/Monitoring...");
    });
    timer->start(100);
}

void MainWindow::setupWorker() {
    thread = new QThread;
    
    worker = new Worker(&indexer);
    
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &Worker::process);
    connect(worker, &Worker::finished, thread, &QThread::quit);
    connect(worker, &Worker::finished, worker, &Worker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}

MainWindow::~MainWindow() {
    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}
