#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <functional>
#include <QListWidgetItem>
#include <QVariantMap>
#include <QStatusBar>

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

    USNIndexer& indexer;
    

    QStatusBar* status_bar = new QStatusBar();
    void onItemClicked(QListWidgetItem* item);

    QThread* thread;
    Worker* worker;
    QTimer* timer;
};

#endif
