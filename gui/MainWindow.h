#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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
    USNIndexer& indexer;
};

#endif
