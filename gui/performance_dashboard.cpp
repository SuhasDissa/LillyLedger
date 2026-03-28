#include "performance_dashboard.h"
#include "ui_performance_dashboard.h"

#include "core/enum/instrument.h"
#include "core/enum/status.h"
#include <QAbstractItemView>
#include <QBrush>
#include <QHeaderView>
#include <QLocale>
#include <QPaintEvent>
#include <QPainter>
#include <QSet>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QTableWidgetItem>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int kMaxHistoryRuns = 20;
constexpr int kInstrumentCount = 5;
constexpr int kTableRowHeight = 36;

QString fixedCharToQString(const char *text, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return QString::fromLatin1(text, static_cast<int>(len));
}

QString instrumentLabel(Instrument instrument) {
    switch (instrument) {
    case Instrument::Rose:
        return "🌹 Rose";
    case Instrument::Lavender:
        return "💜 Lavender";
    case Instrument::Lotus:
        return "🪷 Lotus";
    case Instrument::Tulip:
        return "🌷 Tulip";
    case Instrument::Orchid:
        return "🌸 Orchid";
    }
    return "Unknown";
}

class FillRateProgressDelegate : public QStyledItemDelegate {
  public:
    explicit FillRateProgressDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();
        painter->fillRect(option.rect, option.palette.base());

        const int value = index.data(Qt::UserRole).toInt();
        const QRect trackRect = option.rect.adjusted(8, 8, -8, -8);

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#eee7e3")); // surface-container-high
        painter->drawRoundedRect(trackRect, 2, 2);

        QRect fillRect = trackRect;
        fillRect.setWidth(static_cast<int>(std::round(trackRect.width() * (value / 100.0))));
        if (fillRect.width() > 0) {
            painter->setBrush(QColor("#2c694d")); // secondary
            painter->drawRoundedRect(fillRect, 2, 2);
        }

        painter->setPen(QColor("#1e1b19")); // on-surface
        painter->drawText(trackRect, Qt::AlignCenter, index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

QLabel *buildLegendSwatch(const QString &hexColor, QWidget *parent) {
    auto *swatch = new QLabel(parent);
    swatch->setFixedSize(12, 12);
    swatch->setStyleSheet(
        QString("QLabel { background-color: %1; border-radius: 2px; }").arg(hexColor));
    return swatch;
}
} // namespace

PhaseBreakdownBar::PhaseBreakdownBar(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(40);
    setMaximumHeight(40);
}

void PhaseBreakdownBar::setStats(const PerfStats &stats) {
    stats_ = stats;
    update();
}

void PhaseBreakdownBar::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF barRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor("#d6c3b7"), 1)); // outline-variant
    painter.setBrush(QColor("#eee7e3"));        // surface-container-high
    painter.drawRoundedRect(barRect, 2, 2);

    if (stats_.totalUs <= 0) {
        return;
    }

    const std::array<int64_t, 3> values = {std::max<int64_t>(0, stats_.parseUs),
                                           std::max<int64_t>(0, stats_.matchUs),
                                           std::max<int64_t>(0, stats_.writeUs)};
    const std::array<QString, 3> labels = {"Parse", "Match", "Write"};
    const std::array<QColor, 3> colors = {
        QColor("#84746a"), // outline — Parse
        QColor("#2c694d"), // secondary — Match
        QColor("#86522b")  // primary — Write
    };

    QPainterPath clipPath;
    clipPath.addRoundedRect(barRect, 8, 8);
    painter.save();
    painter.setClipPath(clipPath);

    const double total = static_cast<double>(std::max<int64_t>(1, stats_.totalUs));
    qreal x = barRect.left();
    qreal consumedWidth = 0.0;
    for (int i = 0; i < 3; ++i) {
        qreal segmentWidth = (i == 2) ? (barRect.width() - consumedWidth)
                                      : std::round(barRect.width() * (values[i] / total));
        if (segmentWidth <= 0.0) {
            continue;
        }

        QRectF segmentRect(x, barRect.top(), segmentWidth, barRect.height());
        painter.fillRect(segmentRect, colors[i]);

        if (segmentRect.width() > 60.0) {
            const QString text =
                QString("%1 %2ms").arg(labels[i]).arg(values[i] / 1000.0, 0, 'f', 1);
            painter.setPen(QColor("#ffffff"));
            painter.drawText(segmentRect, Qt::AlignCenter, text);
        }

        x += segmentWidth;
        consumedWidth += segmentWidth;
    }

    painter.restore();
}

PerformanceDashboard::PerformanceDashboard(QWidget *parent)
    : QWidget(parent), ui_(new Ui::PerformanceDashboard) {
    ui_->setupUi(this);

    // KPI card styles
    const QString kpiCardStyle = "QFrame {"
                                 "  background-color: #ffffff;"
                                 "  border: 1px solid #d6c3b7;"
                                 "  border-radius: 6px;"
                                 "}";
    ui_->kpiCard1->setStyleSheet(kpiCardStyle);
    ui_->kpiCard2->setStyleSheet(kpiCardStyle);
    ui_->kpiCard3->setStyleSheet(kpiCardStyle);
    ui_->kpiCard4->setStyleSheet(kpiCardStyle);

    // Phase breakdown container style
    ui_->phaseContainer->setStyleSheet("QFrame {"
                                       "  background-color: #ffffff;"
                                       "  border: 1px solid #d6c3b7;"
                                       "  border-radius: 6px;"
                                       "}");

    // Legend swatches — HTML theme tokens
    ui_->parseSwatch->setStyleSheet("background-color: #84746a; border-radius: 2px;"); // outline
    ui_->matchSwatch->setStyleSheet("background-color: #2c694d; border-radius: 2px;"); // secondary
    ui_->writeSwatch->setStyleSheet("background-color: #86522b; border-radius: 2px;"); // primary

    // Chart setup
    historySeries_ = new QLineSeries(this);
    historySeries_->setColor(QColor("#86522b")); // primary
    historySeries_->setPointsVisible(true);
    QPen seriesPen(QColor("#86522b"));
    seriesPen.setWidth(2);
    historySeries_->setPen(seriesPen);

    auto *chart = new QChart();
    chart->legend()->hide();
    chart->addSeries(historySeries_);
    chart->setBackgroundVisible(true);
    chart->setBackgroundBrush(QBrush(QColor("#ffffff")));
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(QBrush(QColor("#fff8f5")));
    chart->setMargins(QMargins(12, 12, 12, 12));

    axisX_ = new QValueAxis();
    axisX_->setTitleText("Run");
    axisX_->setLabelFormat("%d");
    axisX_->setRange(1, kMaxHistoryRuns);
    axisX_->setGridLineColor(QColor("#d6c3b7"));
    axisX_->setLabelsColor(QColor("#52443c"));
    axisX_->setTitleBrush(QBrush(QColor("#84746a")));

    axisY_ = new QValueAxis();
    axisY_->setTitleText("ms");
    axisY_->setLabelFormat("%.2f");
    axisY_->setRange(0.0, 1.0);
    axisY_->setGridLineColor(QColor("#d6c3b7"));
    axisY_->setLabelsColor(QColor("#52443c"));
    axisY_->setTitleBrush(QBrush(QColor("#84746a")));

    chart->addAxis(axisX_, Qt::AlignBottom);
    chart->addAxis(axisY_, Qt::AlignLeft);
    historySeries_->attachAxis(axisX_);
    historySeries_->attachAxis(axisY_);

    ui_->chartView->setChart(chart);
    ui_->chartView->setRenderHint(QPainter::Antialiasing);
    ui_->chartView->setRubberBand(QChartView::HorizontalRubberBand);
    ui_->chartView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui_->chartView, &QWidget::customContextMenuRequested, this,
            [chart](const QPoint &) { chart->zoomReset(); });
    ui_->chartView->setStyleSheet("QChartView {"
                                  "  background-color: #ffffff;"
                                  "  border: 1px solid #d6c3b7;"
                                  "  border-radius: 6px;"
                                  "}");

    // Instrument table
    ui_->instrumentTable->setHorizontalHeaderLabels(
        {"Instrument", "Orders Routed", "Fill Rate %", "Avg Fill Qty"});
    ui_->instrumentTable->setItemDelegateForColumn(
        2, new FillRateProgressDelegate(ui_->instrumentTable));
    ui_->instrumentTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui_->instrumentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->instrumentTable->verticalHeader()->setVisible(false);
    ui_->instrumentTable->setWordWrap(false);
    ui_->instrumentTable->setShowGrid(false);
    ui_->instrumentTable->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    auto *instrumentHeader = ui_->instrumentTable->horizontalHeader();
    instrumentHeader->setMinimumSectionSize(72);
    instrumentHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    instrumentHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    instrumentHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    instrumentHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui_->instrumentTable->setStyleSheet(
        "QTableWidget {"
        "  background-color: #ffffff;"
        "  color: #1e1b19;"
        "  border: 1px solid #d6c3b7;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "  gridline-color: #eee7e3;"
        "}"
        "QTableWidget::item { padding: 4px 12px; border-bottom: 1px solid #eee7e3; }"
        "QHeaderView::section {"
        "  background-color: #f4ece8;"
        "  color: #52443c;"
        "  border: none;"
        "  border-bottom: 1px solid #d6c3b7;"
        "  border-right: 1px solid #d6c3b7;"
        "  padding: 8px 12px;"
        "  font-weight: 700;"
        "  font-size: 10px;"
        "  letter-spacing: 1px;"
        "  text-transform: uppercase;"
        "}");

    setStyleSheet("PerformanceDashboard { background-color: #fff8f5; }"
                  "QLabel { color: #1e1b19; }");

    updateKpis(PerfStats{});
    updatePhaseLegend(PerfStats{});
    updateHistoryChart();
    updateInstrumentTable({});
    setHasRunData(false);
}

PerformanceDashboard::~PerformanceDashboard() {
    delete ui_;
}

void PerformanceDashboard::updateStats(const PerfStats &stats) {
    setHasRunData(true);
    updateKpis(stats);
    ui_->phaseBar->setStats(stats);
    updatePhaseLegend(stats);
    updateInstrumentTable(latestReports_);
}

void PerformanceDashboard::addRunHistory(const PerfStats &stats) {
    setHasRunData(true);
    updateStats(stats);

    ++runCounter_;
    runHistory_.push_back(RunSample{runCounter_, stats});
    while (static_cast<int>(runHistory_.size()) > kMaxHistoryRuns) {
        runHistory_.pop_front();
    }
    updateHistoryChart();
}

void PerformanceDashboard::updateStats(const PerfStats &stats,
                                       const std::vector<ExecutionReport> &reports) {
    latestReports_ = reports;
    updateStats(stats);
}

void PerformanceDashboard::addRunHistory(const PerfStats &stats,
                                         const std::vector<ExecutionReport> &reports) {
    latestReports_ = reports;
    addRunHistory(stats);
}

void PerformanceDashboard::setHasRunData(bool hasRunData) {
    ui_->stackedWidget->setCurrentIndex(hasRunData ? 1 : 0);
}

void PerformanceDashboard::updateKpis(const PerfStats &stats) {
    const double totalSeconds = static_cast<double>(stats.totalUs) / 1000000.0;
    const double throughput =
        totalSeconds > 0.0 ? static_cast<double>(stats.orderCount) / totalSeconds : 0.0;

    ui_->ordersValueLabel->setText(QLocale().toString(stats.orderCount));
    ui_->reportsValueLabel->setText(QLocale().toString(stats.reportCount));
    ui_->throughputValueLabel->setText(
        QString("%1M orders/sec").arg(throughput / 1000000.0, 0, 'f', 2));
    ui_->totalTimeValueLabel->setText(QString("%1 ms").arg(stats.totalUs / 1000.0, 0, 'f', 2));
}

void PerformanceDashboard::updatePhaseLegend(const PerfStats &stats) {
    ui_->parseLegendValueLabel->setText(formatMicrosecondsAsMs(stats.parseUs));
    ui_->matchLegendValueLabel->setText(formatMicrosecondsAsMs(stats.matchUs));
    ui_->writeLegendValueLabel->setText(formatMicrosecondsAsMs(stats.writeUs));
}

void PerformanceDashboard::updateHistoryChart() {
    historySeries_->clear();

    if (runHistory_.empty()) {
        axisX_->setRange(1, kMaxHistoryRuns);
        axisY_->setRange(0.0, 1.0);
        return;
    }

    double maxMs = 0.0;
    double minMs = std::numeric_limits<double>::max();
    for (const RunSample &sample : runHistory_) {
        const double ms = sample.stats.totalUs / 1000.0;
        historySeries_->append(sample.runNumber, ms);
        maxMs = std::max(maxMs, ms);
        minMs = std::min(minMs, ms);
    }

    const int firstRun = runHistory_.front().runNumber;
    const int lastRun = runHistory_.back().runNumber;

    if (firstRun == lastRun) {
        const int minRun = std::max(1, firstRun - 1);
        axisX_->setRange(minRun, firstRun + 1);
        axisX_->setTickCount((firstRun + 1) - minRun + 1);
    } else {
        axisX_->setRange(firstRun, lastRun);
        axisX_->setTickCount(std::min(5, lastRun - firstRun + 1));
    }

    if (maxMs == minMs) {
        axisY_->setRange(std::max(0.0, minMs - 1.0), maxMs + 1.0);
    } else {
        const double padding = (maxMs - minMs) * 0.15;
        axisY_->setRange(std::max(0.0, minMs - padding), maxMs + padding);
    }
}

void PerformanceDashboard::updateInstrumentTable(const std::vector<ExecutionReport> &reports) {
    struct InstrumentAgg {
        QSet<QString> routedOrders;
        QSet<QString> filledOrders;
        int64_t totalFillQty{0};
    };

    std::array<InstrumentAgg, kInstrumentCount> agg{};
    for (const ExecutionReport &report : reports) {
        const QString orderId = fixedCharToQString(report.orderId, kOrderIdLen);
        if (orderId.isEmpty()) {
            continue;
        }

        const int idx = static_cast<int>(report.instrument);
        if (idx < 0 || idx >= kInstrumentCount) {
            continue;
        }

        agg[static_cast<std::size_t>(idx)].routedOrders.insert(orderId);
        if (report.status == Status::Fill || report.status == Status::PFill) {
            agg[static_cast<std::size_t>(idx)].filledOrders.insert(orderId);
            agg[static_cast<std::size_t>(idx)].totalFillQty += report.quantity;
        }
    }

    for (int row = 0; row < kInstrumentCount; ++row) {
        const auto &a = agg[static_cast<std::size_t>(row)];
        const int routedCount = a.routedOrders.size();
        const int filledCount = a.filledOrders.size();
        const double fillRate = routedCount > 0 ? (100.0 * filledCount / routedCount) : 0.0;
        const double avgFillQty =
            filledCount > 0 ? static_cast<double>(a.totalFillQty) / filledCount : 0.0;

        auto ensureItem = [this, row](int column) {
            QTableWidgetItem *item = ui_->instrumentTable->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem();
                ui_->instrumentTable->setItem(row, column, item);
            }
            return item;
        };

        auto *instrumentItem = ensureItem(0);
        instrumentItem->setText(instrumentLabel(static_cast<Instrument>(row)));

        auto *ordersItem = ensureItem(1);
        ordersItem->setText(QLocale().toString(routedCount));
        ordersItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto *fillRateItem = ensureItem(2);
        fillRateItem->setText(QString("%1%").arg(fillRate, 0, 'f', 1));
        fillRateItem->setData(Qt::UserRole, static_cast<int>(std::round(fillRate)));
        fillRateItem->setTextAlignment(Qt::AlignCenter);

        auto *avgFillItem = ensureItem(3);
        avgFillItem->setText(QLocale().toString(avgFillQty, 'f', 1));
        avgFillItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

QString PerformanceDashboard::formatMicrosecondsAsMs(int64_t us, int precision) {
    return QString("%1 ms").arg(static_cast<double>(us) / 1000.0, 0, 'f', precision);
}