#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QThread;
class Worker;
class QTimer;
class QLabel;
class QString;
class USNIndexer;
class QVBoxLayout;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(USNIndexer& indexer, QWidget* parent = nullptr);
    ~MainWindow();

private:
    void setupWorker();

    USNIndexer& indexer;
    
    QThread* thread;
    Worker* worker;
    QTimer* timer;
};

#endif
