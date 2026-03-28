#pragma once

#include "core/executionreport.h"
#include <QWidget>
#include <cstdint>
#include <deque>
#include <vector>

struct PerfStats {
    int64_t parseUs{0};
    int64_t matchUs{0};
    int64_t writeUs{0};
    int64_t totalUs{0};
    int orderCount{0};
    int reportCount{0};
};

class QLineSeries;
class QValueAxis;
class QChartView;

class PhaseBreakdownBar : public QWidget {
    Q_OBJECT

  public:
    explicit PhaseBreakdownBar(QWidget *parent = nullptr);
    void setStats(const PerfStats &stats);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    PerfStats stats_;
};

namespace Ui {
class PerformanceDashboard;
}

class PerformanceDashboard : public QWidget {
    Q_OBJECT

  public:
    explicit PerformanceDashboard(QWidget *parent = nullptr);
    ~PerformanceDashboard();

  public slots:
    void updateStats(const PerfStats &stats);
    void addRunHistory(const PerfStats &stats);
    void updateStats(const PerfStats &stats, const std::vector<ExecutionReport> &reports);
    void addRunHistory(const PerfStats &stats, const std::vector<ExecutionReport> &reports);

  private:
    struct RunSample {
        int runNumber{0};
        PerfStats stats{};
    };

    void setHasRunData(bool hasRunData);
    void updateKpis(const PerfStats &stats);
    void updatePhaseLegend(const PerfStats &stats);
    void updateHistoryChart();
    void updateInstrumentTable(const std::vector<ExecutionReport> &reports);

    static QString formatMicrosecondsAsMs(int64_t us, int precision = 2);

    Ui::PerformanceDashboard *ui_;
    QLineSeries *historySeries_{nullptr};
    QValueAxis *axisX_{nullptr};
    QValueAxis *axisY_{nullptr};

    std::deque<RunSample> runHistory_;
    int runCounter_{0};
    std::vector<ExecutionReport> latestReports_;
};
