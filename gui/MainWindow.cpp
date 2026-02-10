#include "mainwindow.h"
#include "indexer.hpp"
#include "Worker.h"

#include <QThread>
#include <QDesktopServices>
#include <QLabel>
#include <QUrl>
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
    
    layout->addWidget(button);
    // layout->addWidget(label);
    layout->addWidget(files_list);

    status_bar->addWidget(label);

    setWindowTitle("Findex");
    resize(800, 600);

    setupWorker();

    timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, [this, files_list]() {
        
        std::lock_guard<std::mutex> lock(indexer.dataMutex);

        size_t count = indexer.index_map.size();
        int it_count = 0;
        if (count > 0) {
            for (const auto& it : indexer.index_map) {
                auto& GUI_file_record = it.second;

                QString file_entry_list_text = QString::fromStdString(GUI_file_record.name) + "                " + QString::fromStdString(GUI_file_record.path);
                GUI_file_data["name"] = QString::fromStdString(GUI_file_record.name);
                GUI_file_data["path"] = QString::fromStdString(GUI_file_record.path);

                if (gui_map.find(GUI_file_record.frn) == gui_map.end()) {
                    QListWidgetItem* GUI_file_entry = new QListWidgetItem(file_entry_list_text);
                    GUI_file_entry->setData(Qt::UserRole, GUI_file_data);
                    files_list->addItem(GUI_file_entry);
                    gui_map[GUI_file_record.frn] = GUI_file_entry;
                }
                else {
                    gui_map[GUI_file_record.frn]->setText(file_entry_list_text);
                }

            }
        }
    });
    timer->start(100);

    connect(files_list, &QListWidget::itemDoubleClicked, this, &MainWindow::onItemClicked);


    indexer.onFileRemoved = [this, files_list](uint64_t GUI_removed_file_frn) {
        if (gui_map.find(GUI_removed_file_frn) != gui_map.end()) {
            delete files_list->takeItem(files_list->row(gui_map[GUI_removed_file_frn]));
            gui_map.erase(GUI_removed_file_frn);
        }
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

void MainWindow::onItemClicked(QListWidgetItem* item) { // its double click but i dont want to change function name
    QVariantMap GUI_file_data_get = item->data(Qt::UserRole).toMap();
    QString path = GUI_file_data_get["path"].toString();

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

MainWindow::~MainWindow() {
    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}
