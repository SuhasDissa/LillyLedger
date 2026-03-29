#include "exec_reports_widget.h"
#include "ui_exec_reports_widget.h"

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"
#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QBrush>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextStream>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {
constexpr int kOrderIdColumn = 0;
constexpr int kClientIdColumn = 1;
constexpr int kInstrumentColumn = 2;
constexpr int kSideColumn = 3;
constexpr int kStatusColumn = 4;
constexpr int kQtyColumn = 5;
constexpr int kPriceColumn = 6;
constexpr int kColumnCount = 7;
constexpr int kControlHeight = 34;
constexpr int kTableRowHeight = 42;

constexpr int kAnyFilterValue = -1;

QString fixedCharToQString(const char *text, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return QString::fromLatin1(text, static_cast<int>(len));
}

QString instrumentDisplay(Instrument instrument) {
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

QString statusDisplay(Status status) {
    switch (status) {
    case Status::New:
        return "New";
    case Status::Rejected:
        return "Rejected";
    case Status::Fill:
        return "Fill";
    case Status::PFill:
        return "PFill";
    }
    return "Unknown";
}

QString sideDisplay(Side side) {
    switch (side) {
    case Side::Buy:
        return "BUY";
    case Side::Sell:
        return "SELL";
    }
    return "";
}

QColor statusBackground(Status status) {
    switch (status) {
    case Status::New:
        return QColor("#f7f9fd");
    case Status::Fill:
        return QColor("#f5faf7");
    case Status::PFill:
        return QColor("#fdf8f4");
    case Status::Rejected:
        return QColor("#fdf5f5");
    }
    return QColor("#ffffff");
}

QColor statusForeground(Status status) {
    switch (status) {
    case Status::New:
        return QColor("#5b8fcf");
    case Status::Fill:
        return QColor("#2d6b4a");
    case Status::PFill:
        return QColor("#c2855a");
    case Status::Rejected:
        return QColor("#b84a4a");
    }
    return QColor("#52443c");
}

QString csvEscape(QString field) {
    field.replace('"', "\"\"");
    return QString("\"%1\"").arg(field);
}

struct SummaryCounts {
    int total{0};
    int filled{0};
    int pfilled{0};
    int rejected{0};
    int newResting{0};
};

class SideBadgeDelegate : public QStyledItemDelegate {
  public:
    explicit SideBadgeDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QColor background = option.palette.base().color();
        const QVariant bgVariant = index.data(Qt::BackgroundRole);
        if (bgVariant.canConvert<QBrush>()) {
            background = qvariant_cast<QBrush>(bgVariant).color();
        }
        painter->fillRect(option.rect, background);

        const int side = index.data(Qt::UserRole).toInt();
        const bool isBuy = side == static_cast<int>(Side::Buy);
        const QColor pillColor = isBuy ? QColor("#2f9e44") : QColor("#c92a2a");
        const QColor textColor = QColor("#ffffff");

        const QString text = index.data(Qt::DisplayRole).toString();
        QFont font = option.font;
        font.setBold(true);
        font.setPointSize(std::max(8, font.pointSize() - 1));
        painter->setFont(font);

        const QFontMetrics metrics(font);
        const int pillWidth = std::max(44, metrics.horizontalAdvance(text) + 16);
        const int pillHeight = 20;
        const int left = option.rect.x() + (option.rect.width() - pillWidth) / 2;
        const int top = option.rect.y() + (option.rect.height() - pillHeight) / 2;
        const QRect pillRect(left, top, pillWidth, pillHeight);

        painter->setPen(Qt::NoPen);
        painter->setBrush(pillColor);
        painter->drawRoundedRect(pillRect, 10, 10);

        painter->setPen(textColor);
        painter->drawText(pillRect, Qt::AlignCenter, text);
        painter->restore();
    }
};

} // namespace

class ExecutionReportsModel : public QAbstractTableModel {
  public:
    explicit ExecutionReportsModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) {
            return 0;
        }
        return static_cast<int>(reports_.size());
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) {
            return 0;
        }
        return kColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }

        static const std::array<const char *, kColumnCount> kHeaders = {
            "Order ID",    "Client Order ID", "Instrument", "Side",
            "Exec Status", "Quantity",        "Price"};

        if (section < 0 || section >= kColumnCount) {
            return {};
        }

        return QString::fromLatin1(kHeaders[static_cast<std::size_t>(section)]);
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }

        const ExecutionReport &report = reports_[static_cast<std::size_t>(index.row())];

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case kOrderIdColumn:
                return fixedCharToQString(report.orderId, kOrderIdLen);
            case kClientIdColumn:
                return fixedCharToQString(report.clientOrderId, kClientOrderIdLen);
            case kInstrumentColumn:
                return instrumentDisplay(report.instrument);
            case kSideColumn:
                return sideDisplay(report.side);
            case kStatusColumn:
                return statusDisplay(report.status);
            case kQtyColumn:
                return QLocale().toString(static_cast<qlonglong>(report.quantity));
            case kPriceColumn:
                return QString::number(report.price, 'f', 2);
            default:
                return {};
            }
        }

        if (role == Qt::TextAlignmentRole) {
            if (index.column() == kPriceColumn || index.column() == kQtyColumn) {
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            }
            if (index.column() == kSideColumn || index.column() == kStatusColumn) {
                return static_cast<int>(Qt::AlignCenter);
            }
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }

        if (role == Qt::BackgroundRole) {
            return QBrush(statusBackground(report.status));
        }

        if (role == Qt::ForegroundRole) {
            return QBrush(statusForeground(report.status));
        }

        if (role == Qt::UserRole) {
            if (index.column() == kSideColumn) {
                return static_cast<int>(report.side);
            }
            if (index.column() == kInstrumentColumn) {
                return static_cast<int>(report.instrument);
            }
            if (index.column() == kStatusColumn) {
                return static_cast<int>(report.status);
            }
        }

        return {};
    }

    void setReports(const std::vector<ExecutionReport> &reports) {
        beginResetModel();
        reports_ = reports;
        recomputeSummary();
        endResetModel();
    }

    void appendReport(const ExecutionReport &report) {
        const int row = rowCount();
        beginInsertRows(QModelIndex(), row, row);
        reports_.push_back(report);
        ++summary_.total;
        incrementSummary(report.status);
        endInsertRows();
    }

    const ExecutionReport *reportAt(int row) const {
        if (row < 0 || row >= rowCount()) {
            return nullptr;
        }
        return &reports_[static_cast<std::size_t>(row)];
    }

    int totalCount() const { return static_cast<int>(reports_.size()); }

    SummaryCounts summary() const { return summary_; }

  private:
    void recomputeSummary() {
        summary_ = SummaryCounts{};
        summary_.total = static_cast<int>(reports_.size());
        for (const ExecutionReport &report : reports_) {
            incrementSummary(report.status);
        }
    }

    void incrementSummary(Status status) {
        switch (status) {
        case Status::Fill:
            ++summary_.filled;
            break;
        case Status::PFill:
            ++summary_.pfilled;
            break;
        case Status::Rejected:
            ++summary_.rejected;
            break;
        case Status::New:
            ++summary_.newResting;
            break;
        }
    }

    std::vector<ExecutionReport> reports_;
    SummaryCounts summary_;
};

class ExecutionReportsFilterProxyModel : public QSortFilterProxyModel {
  public:
    explicit ExecutionReportsFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setInstrumentFilter(int instrument) {
        if (instrumentFilter_ == instrument) {
            return;
        }
        instrumentFilter_ = instrument;
        refreshRowsFilter();
    }

    void setStatusFilter(int status) {
        if (statusFilter_ == status) {
            return;
        }
        statusFilter_ = status;
        refreshRowsFilter();
    }

    void setSideFilter(int side) {
        if (sideFilter_ == side) {
            return;
        }
        sideFilter_ = side;
        refreshRowsFilter();
    }

    void setClientOrderIdSearch(const QString &text) {
        const QString normalized = text.trimmed();
        if (clientOrderSearch_ == normalized) {
            return;
        }
        clientOrderSearch_ = normalized;
        refreshRowsFilter();
    }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        Q_UNUSED(sourceParent);

        const auto *reportsModel = dynamic_cast<const ExecutionReportsModel *>(sourceModel());
        if (reportsModel == nullptr) {
            return true;
        }

        const ExecutionReport *report = reportsModel->reportAt(sourceRow);
        if (report == nullptr) {
            return false;
        }

        if (instrumentFilter_ != kAnyFilterValue &&
            static_cast<int>(report->instrument) != instrumentFilter_) {
            return false;
        }

        if (statusFilter_ != kAnyFilterValue && static_cast<int>(report->status) != statusFilter_) {
            return false;
        }

        if (sideFilter_ != kAnyFilterValue && static_cast<int>(report->side) != sideFilter_) {
            return false;
        }

        if (!clientOrderSearch_.isEmpty()) {
            const QString clientId = fixedCharToQString(report->clientOrderId, kClientOrderIdLen);
            if (!clientId.contains(clientOrderSearch_, Qt::CaseInsensitive)) {
                return false;
            }
        }

        return true;
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        const auto *reportsModel = dynamic_cast<const ExecutionReportsModel *>(sourceModel());
        if (reportsModel == nullptr) {
            return QSortFilterProxyModel::lessThan(left, right);
        }

        const ExecutionReport *lhs = reportsModel->reportAt(left.row());
        const ExecutionReport *rhs = reportsModel->reportAt(right.row());
        if (lhs == nullptr || rhs == nullptr) {
            return QSortFilterProxyModel::lessThan(left, right);
        }

        switch (left.column()) {
        case kOrderIdColumn:
            return fixedCharToQString(lhs->orderId, kOrderIdLen) <
                   fixedCharToQString(rhs->orderId, kOrderIdLen);
        case kClientIdColumn:
            return fixedCharToQString(lhs->clientOrderId, kClientOrderIdLen) <
                   fixedCharToQString(rhs->clientOrderId, kClientOrderIdLen);
        case kInstrumentColumn:
            return static_cast<int>(lhs->instrument) < static_cast<int>(rhs->instrument);
        case kSideColumn:
            return static_cast<int>(lhs->side) < static_cast<int>(rhs->side);
        case kStatusColumn:
            return static_cast<int>(lhs->status) < static_cast<int>(rhs->status);
        case kQtyColumn:
            return lhs->quantity < rhs->quantity;
        case kPriceColumn:
            return lhs->price < rhs->price;
        default:
            return QSortFilterProxyModel::lessThan(left, right);
        }
    }

  private:
    void refreshRowsFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(Direction::Rows);
#else
        invalidateFilter();
#endif
    }

    int instrumentFilter_{kAnyFilterValue};
    int statusFilter_{kAnyFilterValue};
    int sideFilter_{kAnyFilterValue};
    QString clientOrderSearch_;
};

ExecutionReportsWidget::ExecutionReportsWidget(QWidget *parent)
    : QWidget(parent), ui_(new Ui::ExecutionReportsWidget) {
    ui_->setupUi(this);
    model_ = new ExecutionReportsModel(this);
    proxyModel_ = new ExecutionReportsFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);

    ui_->tableView->setModel(proxyModel_);
    ui_->tableView->setItemDelegateForColumn(kSideColumn, new SideBadgeDelegate(ui_->tableView));
    ui_->tableView->setSortingEnabled(true);
    ui_->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui_->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui_->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->tableView->setWordWrap(false);
    ui_->tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui_->tableView->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    ui_->tableView->verticalHeader()->setMinimumSectionSize(kTableRowHeight);
    ui_->tableView->verticalHeader()->setVisible(false);
    auto *header = ui_->tableView->horizontalHeader();
    header->setMinimumSectionSize(56);
    header->setSectionResizeMode(kOrderIdColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kClientIdColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kInstrumentColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kSideColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kStatusColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kQtyColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kPriceColumn, QHeaderView::Stretch);
    ui_->tableView->setAlternatingRowColors(false);
    ui_->tableView->setStyleSheet("QTableView {"
                                  " background-color: #ffffff;"
                                  " color: #1e1b19;"
                                  " gridline-color: #f0ebe5;"
                                  " border: 1px solid #e8e2d9;"
                                  "}"
                                  "QHeaderView::section {"
                                  " background-color: #f5f0ea;"
                                  " color: #84746a;"
                                  " border: 1px solid #e8e2d9;"
                                  " padding: 8px 10px;"
                                  "}");

    ui_->instrumentFilter->addItem("All Instruments", kAnyFilterValue);
    ui_->instrumentFilter->addItem("🌹 Rose", static_cast<int>(Instrument::Rose));
    ui_->instrumentFilter->addItem("💜 Lavender", static_cast<int>(Instrument::Lavender));
    ui_->instrumentFilter->addItem("🪷 Lotus", static_cast<int>(Instrument::Lotus));
    ui_->instrumentFilter->addItem("🌷 Tulip", static_cast<int>(Instrument::Tulip));
    ui_->instrumentFilter->addItem("🌸 Orchid", static_cast<int>(Instrument::Orchid));

    ui_->statusFilter->addItem("All Statuses", kAnyFilterValue);
    ui_->statusFilter->addItem("New", static_cast<int>(Status::New));
    ui_->statusFilter->addItem("Rejected", static_cast<int>(Status::Rejected));
    ui_->statusFilter->addItem("Fill", static_cast<int>(Status::Fill));
    ui_->statusFilter->addItem("PFill", static_cast<int>(Status::PFill));

    ui_->sideFilter->addItem("All Sides", kAnyFilterValue);
    ui_->sideFilter->addItem("Buy", static_cast<int>(Side::Buy));
    ui_->sideFilter->addItem("Sell", static_cast<int>(Side::Sell));

    ui_->filledLabel->setStyleSheet("QLabel { color: #2d6b4a; }");
    ui_->partialFilledLabel->setStyleSheet("QLabel { color: #c2855a; }");
    ui_->rejectedLabel->setStyleSheet("QLabel { color: #b84a4a; }");
    ui_->newRestingLabel->setStyleSheet("QLabel { color: #5b8fcf; }");

    connect(ui_->instrumentFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                proxyModel_->setInstrumentFilter(ui_->instrumentFilter->currentData().toInt());
                handleFilterChanged();
            });
    connect(ui_->statusFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        proxyModel_->setStatusFilter(ui_->statusFilter->currentData().toInt());
        handleFilterChanged();
    });
    connect(ui_->sideFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        proxyModel_->setSideFilter(ui_->sideFilter->currentData().toInt());
        handleFilterChanged();
    });
    connect(ui_->searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        proxyModel_->setClientOrderIdSearch(text);
        handleFilterChanged();
    });
    connect(ui_->clearFiltersButton, &QPushButton::clicked, this,
            &ExecutionReportsWidget::clearFilters);
    connect(ui_->exportButton, &QPushButton::clicked, this, &ExecutionReportsWidget::exportCsv);
    connect(ui_->tableView, &QTableView::doubleClicked, this,
            &ExecutionReportsWidget::handleTableDoubleClick);

    setStyleSheet("QWidget { background-color: #fff8f5; color: #1e1b19; font-size: 12px; }"
                  "QComboBox, QLineEdit {"
                  " background-color: #ffffff;"
                  " border: 1px solid #e8e2d9;"
                  " border-radius: 10px;"
                  " padding: 4px 8px;"
                  " min-height: 34px;"
                  " color: #52443c;"
                  "}"
                  "QComboBox::drop-down {"
                  " subcontrol-origin: padding;"
                  " subcontrol-position: top right;"
                  " width: 18px;"
                  " border-left: 1px solid #e8e2d9;"
                  "}"
                  "QComboBox QAbstractItemView {"
                  " background-color: #ffffff;"
                  " color: #52443c;"
                  " border: 1px solid #e8e2d9;"
                  " selection-background-color: #f5f0ea;"
                  " selection-color: #1e1b19;"
                  " outline: 0px;"
                  "}"
                  "QPushButton {"
                  " background-color: #ffffff;"
                  " border: 1px solid #d6c3b7;"
                  " border-radius: 8px;"
                  " color: #52443c;"
                  " padding: 5px 10px;"
                  " min-height: 34px;"
                  "}"
                  "QPushButton:hover { background-color: #fdf8f4; }"
                  "QLabel { color: #52443c; }");
}

void ExecutionReportsWidget::setReports(const std::vector<ExecutionReport> &reports) {
    model_->setReports(reports);
    updateSummaryBar();
    updateShowingLabel();
}

void ExecutionReportsWidget::appendReport(const ExecutionReport &report) {
    model_->appendReport(report);
    updateSummaryBar();
    updateShowingLabel();
}

void ExecutionReportsWidget::clearFilters() {
    ui_->instrumentFilter->setCurrentIndex(0);
    ui_->statusFilter->setCurrentIndex(0);
    ui_->sideFilter->setCurrentIndex(0);
    ui_->searchEdit->clear();
}

void ExecutionReportsWidget::exportCsv() {
    const QString filePath =
        QFileDialog::getSaveFileName(this, "Export CSV", {}, "CSV Files (*.csv)");
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << "Order ID,Client Order ID,Instrument,Side,Exec Status,Quantity,Price\n";

    for (int row = 0; row < proxyModel_->rowCount(); ++row) {
        QStringList fields;
        fields.reserve(kColumnCount);
        for (int col = 0; col < kColumnCount; ++col) {
            const QModelIndex index = proxyModel_->index(row, col);
            fields.push_back(csvEscape(index.data(Qt::DisplayRole).toString()));
        }
        out << fields.join(',') << '\n';
    }
}

void ExecutionReportsWidget::handleTableDoubleClick(const QModelIndex &proxyIndex) {
    if (!proxyIndex.isValid()) {
        return;
    }

    const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
    const ExecutionReport *report = model_->reportAt(sourceIndex.row());
    if (report == nullptr) {
        return;
    }

    emit reportSelected(*report);
}

ExecutionReportsWidget::~ExecutionReportsWidget() {
    delete ui_;
}

void ExecutionReportsWidget::handleFilterChanged() {
    updateShowingLabel();
}

void ExecutionReportsWidget::updateShowingLabel() {
    ui_->showingLabel->setText(
        QString("Showing %1 / %2 reports").arg(proxyModel_->rowCount()).arg(model_->totalCount()));
}

void ExecutionReportsWidget::updateSummaryBar() {
    const SummaryCounts counts = model_->summary();
    ui_->totalReportsLabel->setText(QString("Total reports: %1").arg(counts.total));
    ui_->filledLabel->setText(QString("Filled: %1").arg(counts.filled));
    ui_->partialFilledLabel->setText(QString("Partially filled: %1").arg(counts.pfilled));
    ui_->rejectedLabel->setText(QString("Rejected: %1").arg(counts.rejected));
    ui_->newRestingLabel->setText(QString("New/resting: %1").arg(counts.newResting));
}
