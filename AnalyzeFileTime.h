#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_AnalyzeFileTime.h"

class FileTableModel;

namespace QtCharts {
class QChart;
class QLineSeries;
class QValueAxis;
}

class AnalyzeFileTime : public QMainWindow
{
    Q_OBJECT

public:
    AnalyzeFileTime(QWidget *parent = nullptr);
    ~AnalyzeFileTime();

private:
    void selectFolder();
    void loadFolder();
    void sortFiles(int column, Qt::SortOrder sortOrder);
    void handleSortIndicatorChanged(int column, Qt::SortOrder sortOrder);
    void updateStatistics();
    void updateChart();

    Ui::AnalyzeFileTimeClass ui;
    FileTableModel *fileModel;
    QtCharts::QChart *timeDifferenceChart;
    QtCharts::QLineSeries *timeDifferenceSeries;
    QtCharts::QValueAxis *timeDifferenceAxisX;
    QtCharts::QValueAxis *timeDifferenceAxisY;
    int activeSortColumn;
    Qt::SortOrder activeSortOrder;
};
