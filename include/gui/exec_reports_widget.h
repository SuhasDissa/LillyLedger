#pragma once

#include "qt_metatypes.h"
#include <QWidget>
#include <vector>

class QModelIndex;
class ExecutionReportsModel;
class ExecutionReportsFilterProxyModel;

namespace Ui {
class ExecutionReportsWidget;
}

class ExecutionReportsWidget : public QWidget {
    Q_OBJECT

  public:
    explicit ExecutionReportsWidget(QWidget *parent = nullptr);
    ~ExecutionReportsWidget();

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
    void updateShowingLabel();
    void updateSummaryBar();

    Ui::ExecutionReportsWidget *ui_;
    ExecutionReportsModel *model_{nullptr};
    ExecutionReportsFilterProxyModel *proxyModel_{nullptr};
};
