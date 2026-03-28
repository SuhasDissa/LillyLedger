#include "orderbook_widget.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPropertyAnimation>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>

namespace {
constexpr int kMaxLevels = 10;
constexpr int kTableRowHeight = 34;
constexpr int kDefaultVisibleRows = 4;

constexpr int kBuyPrSeqColumn = 0;
constexpr int kBuyOrderIdColumn = 1;
constexpr int kBuyTraderIdColumn = 2;
constexpr int kBuyQtyColumn = 3;
constexpr int kBuyPriceColumn = 4;

constexpr int kSellPriceColumn = 5;
constexpr int kSellQtyColumn = 6;
constexpr int kSellTraderIdColumn = 7;
constexpr int kSellOrderIdColumn = 8;
constexpr int kSellPrSeqColumn = 9;

QString fixedCharToQString(const char *text, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return QString::fromLatin1(text, static_cast<int>(len));
}

QColor interpolateColor(const QColor &from, const QColor &to, double t) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const int red = static_cast<int>(from.red() + (to.red() - from.red()) * clamped);
    const int green = static_cast<int>(from.green() + (to.green() - from.green()) * clamped);
    const int blue = static_cast<int>(from.blue() + (to.blue() - from.blue()) * clamped);
    return QColor(red, green, blue);
}

class AnimatedBookItem : public QObject, public QTableWidgetItem {
    Q_OBJECT
    Q_PROPERTY(QColor flashColor READ flashColor WRITE setFlashColor)

  public:
    explicit AnimatedBookItem(const QString &text = QString())
        : QObject(nullptr), QTableWidgetItem(text) {}

    QColor flashColor() const { return background().color(); }
    void setFlashColor(const QColor &color) { setBackground(QBrush(color)); }
};

QString tableStyle() {
    // Styles that can't be set globally (column-specific header bg, row hover)
    // Everything else inherits from the app-level stylesheet.
    return "QTableWidget::item:hover {"
           "  background-color: #fdf4ee;"
           "}";
}
} // namespace

OrderBookWidget::OrderBookWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

void OrderBookWidget::updateBook(const std::vector<BookEntry> &buys,
                                 const std::vector<BookEntry> &sells, Instrument instrument) {
    instrumentHeaderLabel_->setText(QString("Orderbook : %1").arg(instrumentLabelText(instrument)));

    const std::vector<BookRow> bidRows = buildBookRows(buys);
    const std::vector<BookRow> askRows = buildBookRows(sells);

    updateSpreadLabel(bidRows, askRows);
    renderBookTable(bidRows, askRows);
}

void OrderBookWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    instrumentHeaderLabel_ = new QLabel("Orderbook : 🌹 Rose", this);
    instrumentHeaderLabel_->setObjectName("OrderBookTitle");
    instrumentHeaderLabel_->setStyleSheet(
        "QLabel#OrderBookTitle {"
        "  color: #1e1b19;"
        "  font-family: 'Georgia', 'Palatino Linotype', serif;"
        "  font-size: 17px;"
        "  font-weight: 400;"
        "  font-style: italic;"
        "  letter-spacing: 0.5px;"
        "}");
    mainLayout->addWidget(instrumentHeaderLabel_);

    spreadLabel_ = new QLabel("Spread: N/A", this);
    spreadLabel_->setStyleSheet(
        "QLabel {"
        "  color: #86522b;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "  font-family: 'Courier New', 'Consolas', monospace;"
        "  background-color: #fdf4ee;"
        "  border: 1px solid #ddc4ae;"
        "  border-radius: 6px;"
        "  padding: 3px 12px;"
        "}");
    mainLayout->addWidget(spreadLabel_);

    bookTable_ = new QTableWidget(this);
    bookTable_->setColumnCount(10);
    bookTable_->setHorizontalHeaderLabels(
        {"Pr.Seq", "Order ID", "Trader ID", "Qty", "Price", "Price", "Qty", "Trader ID",
         "Order ID", "Pr.Seq"});
    setupBookTable(bookTable_);
    mainLayout->addWidget(bookTable_, 1);

    connect(bookTable_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        int priceColumn = -1;
        Side side = Side::Buy;
        if (column <= kBuyPriceColumn) {
            priceColumn = kBuyPriceColumn;
            side = Side::Buy;
        } else {
            priceColumn = kSellPriceColumn;
            side = Side::Sell;
        }

        QTableWidgetItem *priceItem = bookTable_->item(row, priceColumn);
        if (priceItem == nullptr) {
            return;
        }

        bool ok = false;
        const double price = priceItem->data(Qt::UserRole).toDouble(&ok);
        if (!ok || price <= 0.0) {
            return;
        }

        emit priceClicked(price, side);
    });

    setStyleSheet(
        "OrderBookWidget { background-color: #faf8f4; }"
        "QTableWidget {"
        "  background-color: #ffffff;"
        "  border: 1px solid #ead8ce;"
        "  border-radius: 8px;"
        "  gridline-color: #f0e8e2;"
        "  color: #1e1b19;"
        "  font-family: 'Courier New', 'Consolas', monospace;"
        "  font-size: 12px;"
        "}"
        "QHeaderView::section {"
        "  background-color: #f5ede7;"
        "  color: #7a6a5e;"
        "  border: none;"
        "  border-bottom: 1.5px solid #ead8ce;"
        "  border-right: 1px solid #ead8ce;"
        "  padding: 8px 8px;"
        "  font-weight: 700;"
        "  font-size: 10px;"
        "  font-family: 'DM Sans', 'Segoe UI', sans-serif;"
        "  letter-spacing: 0.5px;"
        "}");
}

void OrderBookWidget::setupBookTable(QTableWidget *table) {
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(false);
    table->setWordWrap(false);
    table->setShowGrid(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    table->verticalHeader()->setMinimumSectionSize(kTableRowHeight);

    QFont tableFont = table->font();
    tableFont.setPointSize(11);
    table->setFont(tableFont);

    auto *header = table->horizontalHeader();
    header->setMinimumSectionSize(56);
    for (int col = 0; col < table->columnCount(); ++col) {
        header->setSectionResizeMode(col, QHeaderView::Stretch);

        QTableWidgetItem *headerItem = table->horizontalHeaderItem(col);
        if (headerItem == nullptr) {
            continue;
        }
        headerItem->setTextAlignment(Qt::AlignCenter);
        if (col <= kBuyPriceColumn) {
            headerItem->setBackground(QColor("#2d6b4a")); // deep botanical green — buy side
        } else {
            headerItem->setBackground(QColor("#9e3a3a")); // deep dried rose — sell side
        }
        headerItem->setForeground(QColor("#ffffff"));
    }

    table->setStyleSheet(tableStyle());
}

QString OrderBookWidget::instrumentLabelText(Instrument instrument) {
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

std::vector<OrderBookWidget::BookRow>
OrderBookWidget::buildBookRows(const std::vector<BookEntry> &entries) {
    std::vector<BookRow> rows;
    rows.reserve(std::min(static_cast<int>(entries.size()), kMaxLevels));

    for (int i = 0; i < static_cast<int>(entries.size()) && i < kMaxLevels; ++i) {
        const BookEntry &entry = entries[static_cast<std::size_t>(i)];
        BookRow row;
        row.prioritySeq = static_cast<int>(entry.seqNum) + 1;
        row.orderId = fixedCharToQString(entry.orderId, sizeof(entry.orderId));
        row.traderId = fixedCharToQString(entry.order.clientOrderId, sizeof(entry.order.clientOrderId));
        row.quantity = entry.remainingQty;
        row.price = entry.order.price;
        rows.push_back(row);
    }

    return rows;
}

void OrderBookWidget::renderBookTable(const std::vector<BookRow> &bidRows,
                                      const std::vector<BookRow> &askRows) {
    const int rowCount =
        std::max(kDefaultVisibleRows,
                 std::max(static_cast<int>(bidRows.size()), static_cast<int>(askRows.size())));
    bookTable_->setRowCount(rowCount);

    // Warm editorial palette — green tints for bids, rose tints for asks
    const QColor buyNear("#e8f4ee");   // lightest green (best bid — closest to spread)
    const QColor buyFar("#c8e6d5");    // deeper green (far from spread)
    const QColor buyText("#1a4a30");   // deep botanical green text
    const QColor sellNear("#fdf0f0");  // lightest rose (best ask)
    const QColor sellFar("#f5d4d4");   // deeper rose (far from spread)
    const QColor sellText("#6b1f1f");  // deep dried rose text
    const QColor buyBlank("#f5faf7");  // empty bid cell — very faint green
    const QColor sellBlank("#fdf8f8"); // empty ask cell — very faint rose

    std::map<QString, uint64_t> currentBidQtyByOrderId;
    std::map<QString, uint64_t> currentAskQtyByOrderId;

    auto makeCell = [](const QString &text, const QColor &bg, const QColor &fg,
                       Qt::Alignment alignment) {
        auto *item = new AnimatedBookItem(text);
        item->setBackground(bg);
        item->setForeground(fg);
        item->setTextAlignment(alignment);
        return item;
    };

    for (int row = 0; row < rowCount; ++row) {
        if (row < static_cast<int>(bidRows.size())) {
            const BookRow &bookRow = bidRows[static_cast<std::size_t>(row)];
            currentBidQtyByOrderId[bookRow.orderId] = bookRow.quantity;
            const double t =
                bidRows.size() <= 1 ? 0.0 : static_cast<double>(row) / (bidRows.size() - 1);
            const QColor rowBg = interpolateColor(buyNear, buyFar, t);

            auto *seqItem = makeCell(QString::number(bookRow.prioritySeq), rowBg, buyText, Qt::AlignCenter);
            auto *orderIdItem = makeCell(bookRow.orderId, rowBg, buyText, Qt::AlignCenter);
            auto *traderIdItem = makeCell(bookRow.traderId, rowBg, buyText, Qt::AlignCenter);
            auto *qtyItem = makeCell(formatNumber(bookRow.quantity), rowBg, buyText,
                                     Qt::AlignRight | Qt::AlignVCenter);
            auto *priceItem = makeCell(formatPrice(bookRow.price), rowBg, buyText,
                                       Qt::AlignRight | Qt::AlignVCenter);
            priceItem->setData(Qt::UserRole, bookRow.price);

            bookTable_->setItem(row, kBuyPrSeqColumn, seqItem);
            bookTable_->setItem(row, kBuyOrderIdColumn, orderIdItem);
            bookTable_->setItem(row, kBuyTraderIdColumn, traderIdItem);
            bookTable_->setItem(row, kBuyQtyColumn, qtyItem);
            bookTable_->setItem(row, kBuyPriceColumn, priceItem);

            const auto prev = previousBidQtyByOrderId_.find(bookRow.orderId);
            if (prev != previousBidQtyByOrderId_.end() && prev->second != bookRow.quantity) {
                animateCellsFlash(bookTable_, row, kBuyPrSeqColumn, kBuyPriceColumn, rowBg);
            }
        } else {
            bookTable_->setItem(row, kBuyPrSeqColumn,
                                makeCell("", buyBlank, buyText, Qt::AlignCenter));
            bookTable_->setItem(row, kBuyOrderIdColumn,
                                makeCell("", buyBlank, buyText, Qt::AlignCenter));
            bookTable_->setItem(row, kBuyTraderIdColumn,
                                makeCell("", buyBlank, buyText, Qt::AlignCenter));
            bookTable_->setItem(row, kBuyQtyColumn,
                                makeCell("", buyBlank, buyText, Qt::AlignRight | Qt::AlignVCenter));
            bookTable_->setItem(row, kBuyPriceColumn,
                                makeCell("", buyBlank, buyText, Qt::AlignRight | Qt::AlignVCenter));
        }

        if (row < static_cast<int>(askRows.size())) {
            const BookRow &bookRow = askRows[static_cast<std::size_t>(row)];
            currentAskQtyByOrderId[bookRow.orderId] = bookRow.quantity;
            const double t =
                askRows.size() <= 1 ? 0.0 : static_cast<double>(row) / (askRows.size() - 1);
            const QColor rowBg = interpolateColor(sellNear, sellFar, t);

            auto *priceItem = makeCell(formatPrice(bookRow.price), rowBg, sellText,
                                       Qt::AlignRight | Qt::AlignVCenter);
            priceItem->setData(Qt::UserRole, bookRow.price);
            auto *qtyItem = makeCell(formatNumber(bookRow.quantity), rowBg, sellText,
                                     Qt::AlignRight | Qt::AlignVCenter);
            auto *traderIdItem = makeCell(bookRow.traderId, rowBg, sellText, Qt::AlignCenter);
            auto *orderIdItem = makeCell(bookRow.orderId, rowBg, sellText, Qt::AlignCenter);
            auto *seqItem = makeCell(QString::number(bookRow.prioritySeq), rowBg, sellText, Qt::AlignCenter);

            bookTable_->setItem(row, kSellPriceColumn, priceItem);
            bookTable_->setItem(row, kSellQtyColumn, qtyItem);
            bookTable_->setItem(row, kSellTraderIdColumn, traderIdItem);
            bookTable_->setItem(row, kSellOrderIdColumn, orderIdItem);
            bookTable_->setItem(row, kSellPrSeqColumn, seqItem);

            const auto prev = previousAskQtyByOrderId_.find(bookRow.orderId);
            if (prev != previousAskQtyByOrderId_.end() && prev->second != bookRow.quantity) {
                animateCellsFlash(bookTable_, row, kSellPriceColumn, kSellPrSeqColumn, rowBg);
            }
        } else {
            bookTable_->setItem(row, kSellPriceColumn,
                                makeCell("", sellBlank, sellText, Qt::AlignRight | Qt::AlignVCenter));
            bookTable_->setItem(row, kSellQtyColumn,
                                makeCell("", sellBlank, sellText, Qt::AlignRight | Qt::AlignVCenter));
            bookTable_->setItem(row, kSellTraderIdColumn,
                                makeCell("", sellBlank, sellText, Qt::AlignCenter));
            bookTable_->setItem(row, kSellOrderIdColumn,
                                makeCell("", sellBlank, sellText, Qt::AlignCenter));
            bookTable_->setItem(row, kSellPrSeqColumn,
                                makeCell("", sellBlank, sellText, Qt::AlignCenter));
        }
    }

    previousBidQtyByOrderId_ = std::move(currentBidQtyByOrderId);
    previousAskQtyByOrderId_ = std::move(currentAskQtyByOrderId);
}

void OrderBookWidget::updateSpreadLabel(const std::vector<BookRow> &bidRows,
                                        const std::vector<BookRow> &askRows) {
    if (bidRows.empty() || askRows.empty()) {
        spreadLabel_->setText("Spread: N/A");
        return;
    }

    const double spread = askRows.front().price - bidRows.front().price;
    spreadLabel_->setText(QString("Spread: $%1").arg(spread, 0, 'f', 2));
}

void OrderBookWidget::animateCellsFlash(QTableWidget *table, int row, int firstColumn,
                                        int lastColumn, const QColor &targetColor) {
    for (int col = firstColumn; col <= lastColumn; ++col) {
        auto *item = dynamic_cast<AnimatedBookItem *>(table->item(row, col));
        if (item == nullptr) {
            continue;
        }

        auto *animation = new QPropertyAnimation(item, "flashColor", table);
        animation->setDuration(350);
        animation->setStartValue(QColor("#ffffff"));
        animation->setEndValue(targetColor);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

QString OrderBookWidget::formatPrice(double price) { return QString::number(price, 'f', 2); }

QString OrderBookWidget::formatNumber(uint64_t value) {
    return QLocale().toString(static_cast<qlonglong>(value));
}

#include "orderbook_widget.moc"
