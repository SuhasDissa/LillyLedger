#include "exec_reports_widget.h"

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
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {
constexpr int kTimestampColumn = 0;
constexpr int kClientIdColumn = 1;
constexpr int kOrderIdColumn = 2;
constexpr int kInstrumentColumn = 3;
constexpr int kSideColumn = 4;
constexpr int kPriceColumn = 5;
constexpr int kQtyColumn = 6;
constexpr int kStatusColumn = 7;
constexpr int kReasonColumn = 8;
constexpr int kColumnCount = 9;
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

QString compactTimestamp(const ExecutionReport &report) {
    const QString ts = fixedCharToQString(report.transactTime, kTransactTimeLen);
    if (ts.size() < 19 || ts[8] != '-') {
        return ts;
    }

    const QString hhmmssMs = ts.mid(9);
    if (hhmmssMs.size() < 10) {
        return hhmmssMs;
    }

    return QString("%1:%2:%3").arg(hhmmssMs.mid(0, 2), hhmmssMs.mid(2, 2), hhmmssMs.mid(4, 6));
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
            "Timestamp", "Client ID", "Order ID", "Instrument", "Side",
            "Price",     "Qty",       "Status",   "Reason"};

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
            case kTimestampColumn:
                return compactTimestamp(report);
            case kClientIdColumn:
                return fixedCharToQString(report.clientOrderId, kClientOrderIdLen);
            case kOrderIdColumn:
                return fixedCharToQString(report.orderId, kOrderIdLen);
            case kInstrumentColumn:
                return instrumentDisplay(report.instrument);
            case kSideColumn:
                return sideDisplay(report.side);
            case kPriceColumn:
                return QString::number(report.price, 'f', 2);
            case kQtyColumn:
                return QLocale().toString(static_cast<qlonglong>(report.quantity));
            case kStatusColumn:
                return statusDisplay(report.status);
            case kReasonColumn:
                return fixedCharToQString(report.reason, kReasonLen);
            default:
                return {};
            }
        }

        if (role == Qt::TextAlignmentRole) {
            if (index.column() == kPriceColumn || index.column() == kQtyColumn) {
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
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
        beginFilterChange();
        endFilterChange(Direction::Rows);
    }

    void setStatusFilter(int status) {
        if (statusFilter_ == status) {
            return;
        }
        statusFilter_ = status;
        beginFilterChange();
        endFilterChange(Direction::Rows);
    }

    void setSideFilter(int side) {
        if (sideFilter_ == side) {
            return;
        }
        sideFilter_ = side;
        beginFilterChange();
        endFilterChange(Direction::Rows);
    }

    void setClientOrderIdSearch(const QString &text) {
        const QString normalized = text.trimmed();
        if (clientOrderSearch_ == normalized) {
            return;
        }
        clientOrderSearch_ = normalized;
        beginFilterChange();
        endFilterChange(Direction::Rows);
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
        case kTimestampColumn:
            return fixedCharToQString(lhs->transactTime, kTransactTimeLen) <
                   fixedCharToQString(rhs->transactTime, kTransactTimeLen);
        case kClientIdColumn:
            return fixedCharToQString(lhs->clientOrderId, kClientOrderIdLen) <
                   fixedCharToQString(rhs->clientOrderId, kClientOrderIdLen);
        case kOrderIdColumn:
            return fixedCharToQString(lhs->orderId, kOrderIdLen) <
                   fixedCharToQString(rhs->orderId, kOrderIdLen);
        case kInstrumentColumn:
            return static_cast<int>(lhs->instrument) < static_cast<int>(rhs->instrument);
        case kSideColumn:
            return static_cast<int>(lhs->side) < static_cast<int>(rhs->side);
        case kPriceColumn:
            return lhs->price < rhs->price;
        case kQtyColumn:
            return lhs->quantity < rhs->quantity;
        case kStatusColumn:
            return static_cast<int>(lhs->status) < static_cast<int>(rhs->status);
        case kReasonColumn:
            return fixedCharToQString(lhs->reason, kReasonLen) <
                   fixedCharToQString(rhs->reason, kReasonLen);
        default:
            return QSortFilterProxyModel::lessThan(left, right);
        }
    }

  private:
    int instrumentFilter_{kAnyFilterValue};
    int statusFilter_{kAnyFilterValue};
    int sideFilter_{kAnyFilterValue};
    QString clientOrderSearch_;
};

ExecutionReportsWidget::ExecutionReportsWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

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
    instrumentFilter_->setCurrentIndex(0);
    statusFilter_->setCurrentIndex(0);
    sideFilter_->setCurrentIndex(0);
    searchEdit_->clear();
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
    out << "Timestamp,Client ID,Order ID,Instrument,Side,Price,Qty,Status,Reason\n";

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

void ExecutionReportsWidget::handleFilterChanged() { updateShowingLabel(); }

void ExecutionReportsWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);

    model_ = new ExecutionReportsModel(this);
    proxyModel_ = new ExecutionReportsFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    instrumentFilter_ = new QComboBox(this);
    instrumentFilter_->addItem("All Instruments", kAnyFilterValue);
    instrumentFilter_->addItem("🌹 Rose", static_cast<int>(Instrument::Rose));
    instrumentFilter_->addItem("💜 Lavender", static_cast<int>(Instrument::Lavender));
    instrumentFilter_->addItem("🪷 Lotus", static_cast<int>(Instrument::Lotus));
    instrumentFilter_->addItem("🌷 Tulip", static_cast<int>(Instrument::Tulip));
    instrumentFilter_->addItem("🌸 Orchid", static_cast<int>(Instrument::Orchid));

    statusFilter_ = new QComboBox(this);
    statusFilter_->addItem("All Statuses", kAnyFilterValue);
    statusFilter_->addItem("New", static_cast<int>(Status::New));
    statusFilter_->addItem("Rejected", static_cast<int>(Status::Rejected));
    statusFilter_->addItem("Fill", static_cast<int>(Status::Fill));
    statusFilter_->addItem("PFill", static_cast<int>(Status::PFill));

    sideFilter_ = new QComboBox(this);
    sideFilter_->addItem("All Sides", kAnyFilterValue);
    sideFilter_->addItem("Buy", static_cast<int>(Side::Buy));
    sideFilter_->addItem("Sell", static_cast<int>(Side::Sell));

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search order ID…");

    instrumentFilter_->setMinimumWidth(170);
    statusFilter_->setMinimumWidth(150);
    sideFilter_->setMinimumWidth(120);
    searchEdit_->setMinimumWidth(200);
    instrumentFilter_->setMinimumHeight(kControlHeight);
    statusFilter_->setMinimumHeight(kControlHeight);
    sideFilter_->setMinimumHeight(kControlHeight);
    searchEdit_->setMinimumHeight(kControlHeight);

    clearFiltersButton_ = new QPushButton("Clear Filters", this);
    exportButton_ = new QPushButton("Export CSV", this);
    showingLabel_ = new QLabel("Showing 0 / 0 reports", this);
    showingLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    showingLabel_->setMinimumWidth(190);
    clearFiltersButton_->setMinimumHeight(kControlHeight);
    exportButton_->setMinimumHeight(kControlHeight);

    filterRow->addWidget(instrumentFilter_);
    filterRow->addWidget(statusFilter_);
    filterRow->addWidget(sideFilter_);
    filterRow->addWidget(searchEdit_, 1);
    filterRow->addWidget(clearFiltersButton_);
    filterRow->addStretch();
    filterRow->addWidget(exportButton_);
    filterRow->addWidget(showingLabel_);
    mainLayout->addLayout(filterRow);

    tableView_ = new QTableView(this);
    tableView_->setModel(proxyModel_);
    tableView_->setItemDelegateForColumn(kSideColumn, new SideBadgeDelegate(tableView_));
    tableView_->setSortingEnabled(true);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setWordWrap(false);
    tableView_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableView_->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    tableView_->verticalHeader()->setMinimumSectionSize(kTableRowHeight);
    tableView_->verticalHeader()->setVisible(false);
    auto *header = tableView_->horizontalHeader();
    header->setMinimumSectionSize(56);
    header->setSectionResizeMode(kTimestampColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kClientIdColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kOrderIdColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kInstrumentColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kSideColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kPriceColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kQtyColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kStatusColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kReasonColumn, QHeaderView::Stretch);
    tableView_->setAlternatingRowColors(false);
    tableView_->setStyleSheet(
        "QTableView {"
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
    mainLayout->addWidget(tableView_, 1);

    auto *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(16);

    totalReportsLabel_ = new QLabel("Total reports: 0", this);
    filledLabel_ = new QLabel("Filled: 0", this);
    filledLabel_->setStyleSheet("QLabel { color: #2d6b4a; }");
    partialFilledLabel_ = new QLabel("Partially filled: 0", this);
    partialFilledLabel_->setStyleSheet("QLabel { color: #c2855a; }");
    rejectedLabel_ = new QLabel("Rejected: 0", this);
    rejectedLabel_->setStyleSheet("QLabel { color: #b84a4a; }");
    newRestingLabel_ = new QLabel("New/resting: 0", this);
    newRestingLabel_->setStyleSheet("QLabel { color: #5b8fcf; }");

    summaryRow->addWidget(totalReportsLabel_);
    summaryRow->addWidget(filledLabel_);
    summaryRow->addWidget(partialFilledLabel_);
    summaryRow->addWidget(rejectedLabel_);
    summaryRow->addWidget(newRestingLabel_);
    summaryRow->addStretch();
    mainLayout->addLayout(summaryRow);

    connect(instrumentFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                proxyModel_->setInstrumentFilter(instrumentFilter_->currentData().toInt());
                handleFilterChanged();
            });
    connect(statusFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                proxyModel_->setStatusFilter(statusFilter_->currentData().toInt());
                handleFilterChanged();
            });
    connect(sideFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                proxyModel_->setSideFilter(sideFilter_->currentData().toInt());
                handleFilterChanged();
            });
    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        proxyModel_->setClientOrderIdSearch(text);
        handleFilterChanged();
    });
    connect(clearFiltersButton_, &QPushButton::clicked, this, &ExecutionReportsWidget::clearFilters);
    connect(exportButton_, &QPushButton::clicked, this, &ExecutionReportsWidget::exportCsv);
    connect(tableView_, &QTableView::doubleClicked, this,
            &ExecutionReportsWidget::handleTableDoubleClick);

    setStyleSheet(
        "QWidget { background-color: #fff8f5; color: #1e1b19; font-size: 12px; }"
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

void ExecutionReportsWidget::updateShowingLabel() {
    showingLabel_->setText(
        QString("Showing %1 / %2 reports").arg(proxyModel_->rowCount()).arg(model_->totalCount()));
}

void ExecutionReportsWidget::updateSummaryBar() {
    const SummaryCounts counts = model_->summary();
    totalReportsLabel_->setText(QString("Total reports: %1").arg(counts.total));
    filledLabel_->setText(QString("Filled: %1").arg(counts.filled));
    partialFilledLabel_->setText(QString("Partially filled: %1").arg(counts.pfilled));
    rejectedLabel_->setText(QString("Rejected: %1").arg(counts.rejected));
    newRestingLabel_->setText(QString("New/resting: %1").arg(counts.newResting));
}
