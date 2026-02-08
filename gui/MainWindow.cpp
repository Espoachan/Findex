#include "mainwindow.h"
#include "indexer.hpp"
#include "Worker.h"

#include <QThread>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <QTimer>
#include <QListWidget>
#include <QToolButton>
#include <QApplication>
#include <QDebug>
#include <mutex>

MainWindow::MainWindow(USNIndexer& indexer_ref, QWidget* parent) : QMainWindow(parent), indexer(indexer_ref) 
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *layout = new QVBoxLayout(centralWidget);

    QLabel *label = new QLabel("Yahallo test");
    QListWidget *files_list = new QListWidget();
    QToolButton *button = new QToolButton();
    button->setText("Do nothing");
    
    // files_list->addItem(label->text());
    
    layout->addWidget(button);
    layout->addWidget(label);
    layout->addWidget(files_list);

    /*
    for (int i = 0; i < 10; i++) {
        files_list->addItem("Yahallo " + QString::number(i));
    }
    */

    setWindowTitle("Findex");
    resize(800, 600);
    showMaximized();

    setupWorker();


    timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, [this, files_list]() {
        // qDebug() << "timer running" << indexer.index_map.size();
        
        std::lock_guard<std::mutex> lock(indexer.dataMutex);

        size_t count = indexer.index_map.size();
        
        int it_count = 0;
        if (count > 0) {
            for (const auto& it : indexer.index_map) {
                QString name = QString::fromStdString(it.second.name);

                if (in_list_items.find(name) == in_list_items.end()) {
                    // qDebug() << "This code is indeed executed" << name;
                    files_list->addItem(name);
                    in_list_items.insert(name);
                }

                it_count++;
                if (it_count >= 100) break;
            }
        }

        // TODO: Remove items from list when they are removed from map
        // (removed from map often means that a file was deleted by user)



    });
    timer->start(100);
    applyQSS();
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

void MainWindow::applyQSS() {
    QString qss = R"(
    QMainWindow {
        background-color: #222831;
    }
    QListWidget {
        background-color: #393E46;
        border-radius: 10px;
    }
    QListWidget::item {
        color: white;
    }
    QToolButton {
        background-color: #DFD0B8;
        color: black;
        border-radius: 2px;
        padding: 2px 5px;
    }
    QToolButton:hover {
        background-color: #948979;
        color: white;
    }
    QLabel {
        color: white;
    }
)";

    qApp->setStyleSheet(qss);
}

MainWindow::~MainWindow() {
    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}
