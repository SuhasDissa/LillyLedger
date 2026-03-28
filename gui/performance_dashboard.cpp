#include "performance_dashboard.h"

#include "core/enum/instrument.h"
#include "core/enum/status.h"
#include <QAbstractItemView>
#include <QBrush>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPaintEvent>
#include <QSet>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QStyleOptionProgressBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
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
        painter->setBrush(QColor("#f5f0ea"));
        painter->drawRoundedRect(trackRect, 5, 5);

        QRect fillRect = trackRect;
        fillRect.setWidth(static_cast<int>(std::round(trackRect.width() * (value / 100.0))));
        if (fillRect.width() > 0) {
            painter->setBrush(QColor("#2c694d"));
            painter->drawRoundedRect(fillRect, 5, 5);
        }

        painter->setPen(QColor("#1e1b19"));
        painter->drawText(trackRect, Qt::AlignCenter, index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

QLabel *buildLegendSwatch(const QString &hexColor, QWidget *parent) {
    auto *swatch = new QLabel(parent);
    swatch->setFixedSize(12, 12);
    swatch->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 2px; }").arg(hexColor));
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
    painter.setPen(QPen(QColor("#e8e2d9"), 1));
    painter.setBrush(QColor("#ffffff"));
    painter.drawRoundedRect(barRect, 8, 8);

    if (stats_.totalUs <= 0) {
        return;
    }

    const std::array<int64_t, 3> values = {std::max<int64_t>(0, stats_.parseUs),
                                           std::max<int64_t>(0, stats_.matchUs),
                                           std::max<int64_t>(0, stats_.writeUs)};
    const std::array<QString, 3> labels = {"Parse", "Match", "Write"};
    const std::array<QColor, 3> colors = {QColor("#86522b"), QColor("#2c694d"), QColor("#c2855a")};

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

PerformanceDashboard::PerformanceDashboard(QWidget *parent) : QWidget(parent) { setupUi(); }

void PerformanceDashboard::updateStats(const PerfStats &stats) {
    setHasRunData(true);
    updateKpis(stats);
    phaseBar_->setStats(stats);
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

void PerformanceDashboard::setupUi() {
    stackedLayout_ = new QStackedLayout(this);
    stackedLayout_->setContentsMargins(0, 0, 0, 0);

    placeholderWidget_ = new QWidget(this);
    auto *placeholderLayout = new QVBoxLayout(placeholderWidget_);
    placeholderLayout->setContentsMargins(0, 0, 0, 0);
    placeholderLayout->addStretch();
    auto *emojiLabel = new QLabel("🌸", placeholderWidget_);
    emojiLabel->setAlignment(Qt::AlignCenter);
    emojiLabel->setStyleSheet("QLabel { font-size: 64px; }");
    auto *placeholderText = new QLabel("Run the engine to see performance metrics", placeholderWidget_);
    placeholderText->setAlignment(Qt::AlignCenter);
    placeholderText->setStyleSheet("QLabel { color: #84746a; font-size: 16px; }");
    placeholderLayout->addWidget(emojiLabel);
    placeholderLayout->addWidget(placeholderText);
    placeholderLayout->addStretch();

    contentWidget_ = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(contentWidget_);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    auto *kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(12);
    kpiRow->addWidget(buildKpiCard("Orders Processed", &ordersValueLabel_, "this run"));
    kpiRow->addWidget(buildKpiCard("Reports Generated", &reportsValueLabel_, "this run"));
    kpiRow->addWidget(buildKpiCard("Throughput", &throughputValueLabel_, "orders/sec"));
    kpiRow->addWidget(buildKpiCard("Total Time", &totalTimeValueLabel_, "this run"));
    mainLayout->addLayout(kpiRow);

    auto *phaseContainer = new QFrame(contentWidget_);
    phaseContainer->setStyleSheet(
        "QFrame { background-color: #ffffff; border: 1px solid #e8e2d9; border-radius: 10px; }");
    auto *phaseLayout = new QVBoxLayout(phaseContainer);
    phaseLayout->setContentsMargins(10, 10, 10, 10);
    phaseLayout->setSpacing(8);
    phaseBar_ = new PhaseBreakdownBar(phaseContainer);
    phaseLayout->addWidget(phaseBar_);

    auto *legendRow = new QHBoxLayout();
    legendRow->setSpacing(16);

    auto addLegendItem = [phaseContainer, legendRow](const QString &name, const QString &color,
                                                      QLabel **valueLabel) {
        auto *itemLayout = new QHBoxLayout();
        itemLayout->setSpacing(6);
        itemLayout->addWidget(buildLegendSwatch(color, phaseContainer));
        auto *nameLabel = new QLabel(name, phaseContainer);
        nameLabel->setStyleSheet("QLabel { color: #52443c; }");
        *valueLabel = new QLabel("0.00 ms", phaseContainer);
        (*valueLabel)->setStyleSheet("QLabel { color: #1e1b19; font-weight: 600; }");
        itemLayout->addWidget(nameLabel);
        itemLayout->addWidget(*valueLabel);
        itemLayout->addStretch();
        legendRow->addLayout(itemLayout);
    };

    addLegendItem("Parse", "#86522b", &parseLegendValueLabel_);
    addLegendItem("Match", "#2c694d", &matchLegendValueLabel_);
    addLegendItem("Write", "#c2855a", &writeLegendValueLabel_);
    legendRow->addStretch();
    phaseLayout->addLayout(legendRow);
    mainLayout->addWidget(phaseContainer);

    historySeries_ = new QLineSeries(contentWidget_);
    historySeries_->setColor(QColor("#c2855a"));

    auto *chart = new QChart();
    chart->legend()->hide();
    chart->addSeries(historySeries_);
    chart->setBackgroundVisible(true);
    chart->setBackgroundBrush(QBrush(QColor("#fff8f5")));
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(QBrush(QColor("#ffffff")));
    chart->setMargins(QMargins(8, 8, 8, 8));

    axisX_ = new QValueAxis();
    axisX_->setTitleText("Run");
    axisX_->setLabelFormat("%d");
    axisX_->setRange(1, kMaxHistoryRuns);
    axisX_->setGridLineColor(QColor("#e8e2d9"));
    axisX_->setLabelsColor(QColor("#52443c"));
    axisX_->setTitleBrush(QBrush(QColor("#52443c")));

    axisY_ = new QValueAxis();
    axisY_->setTitleText("ms");
    axisY_->setLabelFormat("%.2f");
    axisY_->setRange(0.0, 1.0);
    axisY_->setGridLineColor(QColor("#e8e2d9"));
    axisY_->setLabelsColor(QColor("#52443c"));
    axisY_->setTitleBrush(QBrush(QColor("#52443c")));

    chart->addAxis(axisX_, Qt::AlignBottom);
    chart->addAxis(axisY_, Qt::AlignLeft);
    historySeries_->attachAxis(axisX_);
    historySeries_->attachAxis(axisY_);

    chartView_ = new QChartView(chart, contentWidget_);
    chartView_->setRenderHint(QPainter::Antialiasing);
    chartView_->setRubberBand(QChartView::HorizontalRubberBand);
    chartView_->setMinimumHeight(250);
    chartView_->setStyleSheet("QChartView { background-color: #ffffff; border: 1px solid #e8e2d9; }");
    mainLayout->addWidget(chartView_, 1);

    instrumentTable_ = new QTableWidget(kInstrumentCount, 4, contentWidget_);
    instrumentTable_->setHorizontalHeaderLabels(
        {"Instrument", "Orders Routed", "Fill Rate %", "Avg Fill Qty"});
    instrumentTable_->setItemDelegateForColumn(2, new FillRateProgressDelegate(instrumentTable_));
    instrumentTable_->setSelectionMode(QAbstractItemView::NoSelection);
    instrumentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    instrumentTable_->verticalHeader()->setVisible(false);
    instrumentTable_->setWordWrap(false);
    instrumentTable_->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    auto *instrumentHeader = instrumentTable_->horizontalHeader();
    instrumentHeader->setMinimumSectionSize(72);
    instrumentHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    instrumentHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    instrumentHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    instrumentHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    instrumentTable_->setMinimumHeight(220);
    instrumentTable_->setStyleSheet(
        "QTableWidget { background-color: #ffffff; color: #1e1b19; border: 1px solid #e8e2d9; }"
        "QHeaderView::section { background-color: #f5f0ea; color: #52443c; border: 1px solid #e8e2d9; padding: 8px 10px; }");
    mainLayout->addWidget(instrumentTable_);

    stackedLayout_->addWidget(placeholderWidget_);
    stackedLayout_->addWidget(contentWidget_);

    setStyleSheet("QWidget { background-color: #fff8f5; color: #1e1b19; }");

    updateKpis(PerfStats{});
    updatePhaseLegend(PerfStats{});
    updateHistoryChart();
    updateInstrumentTable({});
    setHasRunData(false);
}

QFrame *PerformanceDashboard::buildKpiCard(const QString &title, QLabel **valueLabel,
                                           const QString &subtitle) {
    auto *card = new QFrame(contentWidget_);
    card->setStyleSheet(
        "QFrame { background-color: #ffffff; border: 1px solid #e8e2d9; border-radius: 10px; }");

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(4);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet("QLabel { color: #84746a; font-size: 12px; font-weight: 600; }");

    *valueLabel = new QLabel("0", card);
    (*valueLabel)->setStyleSheet("QLabel { color: #1e1b19; font-size: 28px; font-weight: 700; }");

    auto *subtitleLabel = new QLabel(subtitle, card);
    subtitleLabel->setStyleSheet("QLabel { color: #84746a; font-size: 11px; }");

    layout->addWidget(titleLabel);
    layout->addWidget(*valueLabel);
    layout->addWidget(subtitleLabel);
    return card;
}

void PerformanceDashboard::setHasRunData(bool hasRunData) {
    stackedLayout_->setCurrentWidget(hasRunData ? contentWidget_ : placeholderWidget_);
}

void PerformanceDashboard::updateKpis(const PerfStats &stats) {
    const double totalSeconds = static_cast<double>(stats.totalUs) / 1000000.0;
    const double throughput =
        totalSeconds > 0.0 ? static_cast<double>(stats.orderCount) / totalSeconds : 0.0;

    ordersValueLabel_->setText(QLocale().toString(stats.orderCount));
    reportsValueLabel_->setText(QLocale().toString(stats.reportCount));
    throughputValueLabel_->setText(QString("%1M orders/sec").arg(throughput / 1000000.0, 0, 'f', 2));
    totalTimeValueLabel_->setText(QString("%1 ms").arg(stats.totalUs / 1000.0, 0, 'f', 2));
}

void PerformanceDashboard::updatePhaseLegend(const PerfStats &stats) {
    parseLegendValueLabel_->setText(formatMicrosecondsAsMs(stats.parseUs));
    matchLegendValueLabel_->setText(formatMicrosecondsAsMs(stats.matchUs));
    writeLegendValueLabel_->setText(formatMicrosecondsAsMs(stats.writeUs));
}

void PerformanceDashboard::updateHistoryChart() {
    historySeries_->clear();

    if (runHistory_.empty()) {
        axisX_->setRange(1, kMaxHistoryRuns);
        axisY_->setRange(0.0, 1.0);
        return;
    }

    double maxMs = 0.0;
    for (const RunSample &sample : runHistory_) {
        const double ms = sample.stats.totalUs / 1000.0;
        historySeries_->append(sample.runNumber, ms);
        maxMs = std::max(maxMs, ms);
    }

    const int firstRun = runHistory_.front().runNumber;
    const int lastRun = runHistory_.back().runNumber;
    axisX_->setRange(firstRun, lastRun);

    const double paddedMax = std::max(1.0, maxMs * 1.15);
    axisY_->setRange(0.0, paddedMax);
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
        const double avgFillQty = filledCount > 0 ? static_cast<double>(a.totalFillQty) / filledCount : 0.0;

        auto ensureItem = [this, row](int column) {
            QTableWidgetItem *item = instrumentTable_->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem();
                instrumentTable_->setItem(row, column, item);
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
