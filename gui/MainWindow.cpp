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
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* main_layout = new QVBoxLayout(centralWidget);

    QLabel* test_label = new QLabel("Yahallo   ");
    QLabel* total_files_label = new QLabel();
    QLabel* indexing_label = new QLabel("   Indexing...    ");

    QListWidget* files_list = new QListWidget();
    QToolButton* test_button = new QToolButton();
    test_button->setText("Do nothing");

    // main_layout->addWidget(test_button);
    main_layout->addWidget(files_list);
    main_layout->addWidget(search_bar);
    
    setStatusBar(status_bar);
    status_bar->addWidget(test_label);
    status_bar->addWidget(total_files_label);
    status_bar->addWidget(indexing_label);

    search_bar->setPlaceholderText("Search");

    setWindowTitle("Findex");
    resize(800, 600);

    setupWorker();

    const int max_files_entries_list = 1000;

    timer = new QTimer(this);

    connect(search_bar, &QLineEdit::textChanged, this, [this, files_list](const QString& text) {
        std::lock_guard<std::mutex> lock(indexer.dataMutex);

        files_list->clear();
        search_gui_map.clear();
        search_it_count = 0;
        gui_map.clear();
        it_count = 0;

        timer->stop();

        std::string search_term = text.toLower().toStdString();

        for (const auto& [frn, record] : indexer.index_map) {
            std::string name = record.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return std::tolower(c); 
            });

            if (!search_term.empty() && name.find(search_term) == std::string::npos) {
                continue; 
            }
            else if (search_term.empty()) {
                timer->start(100);
                return;
            }

            if (search_it_count > 1000) break;

            QString file_entry_list_text = QString::fromStdString(record.name) + "    " + QString::fromStdString(record.path);

            QListWidgetItem* entry = new QListWidgetItem(file_entry_list_text);

            QVariantMap data;
            data["path"] = QString::fromStdString(record.path);
            entry->setData(Qt::UserRole, data);

            files_list->addItem(entry);
            search_gui_map[frn] = entry;

            ++search_it_count;
        }
    });


    connect(worker, &Worker::finishedFullIndex, this, [this, indexing_label]() {
        status_bar->removeWidget(indexing_label);
    });

    connect(timer, &QTimer::timeout, this, [this, total_files_label, max_files_entries_list, files_list]() {
        std::lock_guard<std::mutex> lock(indexer.dataMutex);

        size_t count = indexer.index_map.size();

        total_files_label->setText("    " + QString::number(static_cast<unsigned long>(count)) + "    ");

        if (count == 0) return;
        if (it_count < max_files_entries_list) {
            for (const auto& it : indexer.index_map) {
                auto& GUI_file_record = it.second;

                if (QString::fromStdString(GUI_file_record.path).isEmpty()) break;

                QString file_entry_list_text = QString::fromStdString(GUI_file_record.name) + "                " + QString::fromStdString(GUI_file_record.path);
                GUI_file_data["name"] = QString::fromStdString(GUI_file_record.name);
                GUI_file_data["path"] = QString::fromStdString(GUI_file_record.path);

                if (gui_map.find(GUI_file_record.frn) == gui_map.end()) {
                    if (it_count > max_files_entries_list) break;
                    QListWidgetItem* GUI_file_entry = new QListWidgetItem(file_entry_list_text);
                    GUI_file_entry->setData(Qt::UserRole, GUI_file_data);
                    files_list->addItem(GUI_file_entry);
                    gui_map[GUI_file_record.frn] = GUI_file_entry;

                    ++it_count;
                    // qDebug() << it_count;
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
            --it_count;
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
        background-color: #1e2128;
    }

    QListWidget{
        background-color: #262a34;
        border-radius: 8px;
        border: 1px solid #2d303a;
        padding: 5px 10px;
    }
    QLineEdit {
        background-color: #23262d;
        color: white;
        border-radius: 8px;
        border: 1px solid #2d303a;
        padding: 4px 8px;
    }
    QToolButton {
        background-color: #23262d;
        color: white;
        border-radius: 8px;
        border: 1px solid #2d303a;
        padding: 4px 8px;
    }
    QToolButton:hover {
        background-color: #2d303a;
        color: white;
    }
)";

    qApp->setStyleSheet(qss);
}

void MainWindow::onItemClicked(QListWidgetItem* item) { // its double click but i dont want to change function name
    QVariantMap GUI_file_data_get = item->data(Qt::UserRole).toMap();
    QString path = GUI_file_data_get["path"].toString();

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::showIndexedEntries(QLabel *total_files_label, int max_files_entries_list, QListWidget *files_list) {
    std::lock_guard<std::mutex> lock(indexer.dataMutex);

    size_t count = indexer.index_map.size();

    total_files_label->setText("    " + QString::number(static_cast<unsigned long>(count)) + "    ");

    if (count == 0) return;
    if (it_count < max_files_entries_list) {
        for (const auto& it : indexer.index_map) {
            auto& GUI_file_record = it.second;

            if (QString::fromStdString(GUI_file_record.path).isEmpty()) break;

            QString file_entry_list_text = QString::fromStdString(GUI_file_record.name) + "                " + QString::fromStdString(GUI_file_record.path);
            GUI_file_data["name"] = QString::fromStdString(GUI_file_record.name);
            GUI_file_data["path"] = QString::fromStdString(GUI_file_record.path);

            if (gui_map.find(GUI_file_record.frn) == gui_map.end()) {
                if (it_count > max_files_entries_list) break;
                QListWidgetItem* GUI_file_entry = new QListWidgetItem(file_entry_list_text);
                GUI_file_entry->setData(Qt::UserRole, GUI_file_data);
                files_list->addItem(GUI_file_entry);
                gui_map[GUI_file_record.frn] = GUI_file_entry;

                ++it_count;
                // qDebug() << it_count;
            }
            else {
                gui_map[GUI_file_record.frn]->setText(file_entry_list_text);
            }

        }

    }
}

MainWindow::~MainWindow() {
    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}
