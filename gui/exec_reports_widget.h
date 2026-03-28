#pragma once

#include "qt_metatypes.h"
#include <QWidget>
#include <vector>

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableView;
class QModelIndex;

class ExecutionReportsModel;
class ExecutionReportsFilterProxyModel;

class ExecutionReportsWidget : public QWidget {
    Q_OBJECT

  public:
    explicit ExecutionReportsWidget(QWidget *parent = nullptr);

  public slots:
    void setReports(const std::vector<ExecutionReport> &reports);
    void appendReport(const ExecutionReport &report);

  signals:
    void reportSelected(const ExecutionReport &report);

  private slots:
    void clearFilters();
    void exportCsv();
    void handleTableDoubleClick(const QModelIndex &proxyIndex);
    void handleFilterChanged();

  private:
    void setupUi();
    void updateShowingLabel();
    void updateSummaryBar();

    QComboBox *instrumentFilter_{nullptr};
    QComboBox *statusFilter_{nullptr};
    QComboBox *sideFilter_{nullptr};
    QLineEdit *searchEdit_{nullptr};
    QPushButton *clearFiltersButton_{nullptr};
    QPushButton *exportButton_{nullptr};
    QLabel *showingLabel_{nullptr};

    QTableView *tableView_{nullptr};

    QLabel *totalReportsLabel_{nullptr};
    QLabel *filledLabel_{nullptr};
    QLabel *partialFilledLabel_{nullptr};
    QLabel *rejectedLabel_{nullptr};
    QLabel *newRestingLabel_{nullptr};

    ExecutionReportsModel *model_{nullptr};
    ExecutionReportsFilterProxyModel *proxyModel_{nullptr};
};
