#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QString;
class USNIndexer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(USNIndexer& indexer, QWidget* parent = nullptr);
    ~MainWindow();

    void updateLabels();

private:
    USNIndexer& indexer;
    QLabel* file_name_label;
};

#endif
