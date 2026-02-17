#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <functional>
#include <QListWidgetItem>
#include <QVariantMap>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QLineEdit>

class QThread;
class Worker;
class QTimer;
class QLabel;
class QString;
class QDesktopServices;
class QUrl;
class USNIndexer;
class QVBoxLayout;
class QWidget;
class QListWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(USNIndexer& indexer, QWidget* parent = nullptr);
    ~MainWindow();

    QVariantMap GUI_file_data;

private:
    void setupWorker();

    void applyQSS();
    std::set<uint64_t> in_list_items;
    std::unordered_map<uint64_t, QListWidgetItem*> gui_map;
    std::unordered_map<uint64_t, QListWidgetItem*> search_gui_map;


    USNIndexer& indexer;
    int it_count = 0;
    int search_it_count = 0;

    QLineEdit* search_bar = new QLineEdit();
    QStatusBar* status_bar = new QStatusBar();

    QMenuBar* menu_bar = new QMenuBar();

    void showIndexedEntries(QLabel *total_files_label, int max_files_entries_list, QListWidget* files_list);
    void onItemClicked(QListWidgetItem* item);

    QThread* thread;
    Worker* worker;
    QTimer* search_timer;
    QTimer* timer;
};

#endif
