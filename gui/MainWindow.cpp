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
    QListWidget* files_list = new QListWidget();
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
                auto& GUI_file_record = it.second;

                QString file_entry_list_text = QString::fromStdString(GUI_file_record.name) + "                " + QString::fromStdString(GUI_file_record.path);
                /*
                QString name = QString::fromStdString(it.second.name);
                uint64_t frn = it.second.frn;
                QString old_name = QString::fromStdString(it.second.old_name);
                */
                if (gui_map.find(GUI_file_record.frn) == gui_map.end()) {
                    QListWidgetItem* GUI_file_entry = new QListWidgetItem(file_entry_list_text);
                    files_list->addItem(GUI_file_entry);
                    gui_map[GUI_file_record.frn] = GUI_file_entry;
                }
                else {
                    gui_map[GUI_file_record.frn]->setText(file_entry_list_text);
                }

                /*
                if (in_list_items.find(frn) == in_list_items.end()) {
                    qDebug() << "This code is indeed executed" << frn;
                    files_list->addItem(name + "                " + old_name);
                    in_list_items.insert(frn);
                }
                else {
                    qDebug() << "HELPF LPEFPE KFPKEF P";
                }

                it_count++;
                if (it_count >= 100) break;
                */
            }
        }

        // TODO: Remove items from list when they are removed from map
        // (removed from map often means that a file was deleted by user)



    });


    timer->start(100);

    indexer.onFileRemoved = [this, files_list](uint64_t GUI_removed_file_frn) {
        // qDebug() << "This is executing";


        //         QList<QListWidgetItem*> items = files_list->findItems(file_to_delete_from_list_name, Qt::MatchContains);
        /*
        for (QListWidgetItem* item : items) {
            delete files_list->takeItem(files_list->row(item));
        }

        */

        if (gui_map.find(GUI_removed_file_frn) != gui_map.end()) {
            delete files_list->takeItem(files_list->row(gui_map[GUI_removed_file_frn]));
            gui_map.erase(GUI_removed_file_frn);
        }

        qDebug() << "File deleted" << GUI_removed_file_frn;
        };

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
        background-color: #1E1E2F;
    }
    QListWidget {
        background-color: #2E2E3E;
        border-radius: 8px;
        padding: 5px 10px;
    }
    QListWidget::item {
        color: #E0E0E0;
        padding: 5px 5px;
    }

    QListWidget::item:selected {
        background-color: #1E1E2F;
        border-radius: 4px;
        color: white;
    }
    QToolButton {
        background-color: #4E4E60;
        color: #FFFFFF;
        border-radius: 4px;
        padding: 4px 8px;
    }
    QToolButton:hover {
        background-color: #6E6E80;
        color: white;
    }
    QLabel {
        color: white;
    }
)";

    // qApp->setStyleSheet(qss);
}

MainWindow::~MainWindow() {
    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}
