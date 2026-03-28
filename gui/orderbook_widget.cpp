#include "orderbook_widget.h"

#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPropertyAnimation>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>

namespace {
constexpr int kMaxLevels = 10;
constexpr int kPriceColumn = 0;
constexpr int kQtyColumn = 1;
constexpr int kOrdersColumn = 2;
constexpr int kCumQtyColumn = 3;

class AnimatedBookItem : public QObject, public QTableWidgetItem {
    Q_OBJECT
    Q_PROPERTY(QColor flashColor READ flashColor WRITE setFlashColor)

  public:
    explicit AnimatedBookItem(const QString &text = QString())
        : QObject(nullptr), QTableWidgetItem(text) {}

    QColor flashColor() const { return background().color(); }

    void setFlashColor(const QColor &color) { setBackground(QBrush(color)); }
};

QString sideTableStyle() {
    return "QTableWidget {"
           " background-color: #2a2a3e;"
           " color: #cdd6f4;"
           " border: 1px solid #45475a;"
           " gridline-color: #313244;"
           "}"
           "QHeaderView::section {"
           " background-color: #1e1e2e;"
           " color: #cdd6f4;"
           " border: 1px solid #45475a;"
           " padding: 4px 6px;"
           "}";
}
} // namespace

OrderBookWidget::OrderBookWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

void OrderBookWidget::updateBook(const std::vector<BookEntry> &buys,
                                 const std::vector<BookEntry> &sells, Instrument instrument) {
    instrumentHeaderLabel_->setText(instrumentLabelText(instrument));

    const std::vector<PriceLevel> bidLevels = buildBuyLevels(buys);
    const std::vector<PriceLevel> askLevelsAscending = buildSellLevelsAscending(sells);

    updateSpreadLabel(bidLevels, askLevelsAscending);
    renderAsks(askLevelsAscending);
    renderBids(bidLevels);
}

void OrderBookWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    instrumentHeaderLabel_ = new QLabel(instrumentLabelText(Instrument::Rose), this);
    instrumentHeaderLabel_->setStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; font-weight: 700; }");
    mainLayout->addWidget(instrumentHeaderLabel_);

    spreadLabel_ = new QLabel("Spread: N/A", this);
    spreadLabel_->setStyleSheet("QLabel { color: #f9e2af; font-weight: 600; }");
    mainLayout->addWidget(spreadLabel_);

    ladderSplitter_ = new QSplitter(Qt::Vertical, this);
    ladderSplitter_->setChildrenCollapsible(false);
    ladderSplitter_->setHandleWidth(2);
    ladderSplitter_->setStyleSheet("QSplitter::handle { background-color: #585b70; }");

    asksTable_ = new QTableWidget(ladderSplitter_);
    setupLadderTable(asksTable_);

    bidsTable_ = new QTableWidget(ladderSplitter_);
    setupLadderTable(bidsTable_);

    ladderSplitter_->addWidget(asksTable_);
    ladderSplitter_->addWidget(bidsTable_);
    ladderSplitter_->setStretchFactor(0, 1);
    ladderSplitter_->setStretchFactor(1, 1);
    mainLayout->addWidget(ladderSplitter_, 1);

    connect(asksTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto *priceItem = asksTable_->item(row, kPriceColumn);
        if (priceItem == nullptr) {
            return;
        }
        emit priceClicked(priceItem->data(Qt::UserRole).toDouble(), Side::Sell);
    });

    connect(bidsTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto *priceItem = bidsTable_->item(row, kPriceColumn);
        if (priceItem == nullptr) {
            return;
        }
        emit priceClicked(priceItem->data(Qt::UserRole).toDouble(), Side::Buy);
    });

    setStyleSheet("QWidget { background-color: #1e1e2e; }");
}

void OrderBookWidget::setupLadderTable(QTableWidget *table) {
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"Price", "Qty", "Orders", "Cumulative Qty"});
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->setColumnWidth(kPriceColumn, 100);
    table->setColumnWidth(kQtyColumn, 80);
    table->setColumnWidth(kOrdersColumn, 60);
    table->setColumnWidth(kCumQtyColumn, 110);
    table->setStyleSheet(sideTableStyle());
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

QColor OrderBookWidget::interpolateColor(const QColor &from, const QColor &to, double t) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const int red = static_cast<int>(from.red() + (to.red() - from.red()) * clamped);
    const int green = static_cast<int>(from.green() + (to.green() - from.green()) * clamped);
    const int blue = static_cast<int>(from.blue() + (to.blue() - from.blue()) * clamped);
    return QColor(red, green, blue);
}

std::vector<OrderBookWidget::PriceLevel>
OrderBookWidget::buildBuyLevels(const std::vector<BookEntry> &buys) {
    struct Bucket {
        uint64_t qty{0};
        int orders{0};
    };

    std::map<double, Bucket, std::greater<double>> aggregated;
    for (const BookEntry &entry : buys) {
        auto &bucket = aggregated[entry.order.price];
        bucket.qty += entry.remainingQty;
        bucket.orders += 1;
    }

    std::vector<PriceLevel> levels;
    levels.reserve(kMaxLevels);

    uint64_t cumulative = 0;
    for (const auto &[price, bucket] : aggregated) {
        cumulative += bucket.qty;
        levels.push_back(PriceLevel{price, bucket.qty, bucket.orders, cumulative});
        if (static_cast<int>(levels.size()) >= kMaxLevels) {
            break;
        }
    }

    return levels;
}

std::vector<OrderBookWidget::PriceLevel>
OrderBookWidget::buildSellLevelsAscending(const std::vector<BookEntry> &sells) {
    struct Bucket {
        uint64_t qty{0};
        int orders{0};
    };

    std::map<double, Bucket> aggregated;
    for (const BookEntry &entry : sells) {
        auto &bucket = aggregated[entry.order.price];
        bucket.qty += entry.remainingQty;
        bucket.orders += 1;
    }

    std::vector<PriceLevel> levels;
    levels.reserve(kMaxLevels);

    uint64_t cumulative = 0;
    for (const auto &[price, bucket] : aggregated) {
        cumulative += bucket.qty;
        levels.push_back(PriceLevel{price, bucket.qty, bucket.orders, cumulative});
        if (static_cast<int>(levels.size()) >= kMaxLevels) {
            break;
        }
    }

    return levels;
}

void OrderBookWidget::renderAsks(const std::vector<PriceLevel> &askLevelsAscending) {
    std::vector<PriceLevel> displayLevels = askLevelsAscending;
    std::reverse(displayLevels.begin(), displayLevels.end());

    asksTable_->setRowCount(static_cast<int>(displayLevels.size()));

    const QColor textColor("#f38ba8");
    const QColor deepRed("#58253a");
    const QColor lightRed("#74405a");
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    std::map<double, uint64_t> currentQtyByPrice;

    for (int row = 0; row < static_cast<int>(displayLevels.size()); ++row) {
        const PriceLevel &level = displayLevels[static_cast<std::size_t>(row)];
        currentQtyByPrice[level.price] = level.quantity;

        const double t = (displayLevels.size() <= 1)
                             ? 1.0
                             : static_cast<double>(row) / (displayLevels.size() - 1);
        const QColor rowColor = interpolateColor(deepRed, lightRed, t);

        auto *priceItem = new AnimatedBookItem(formatPrice(level.price));
        priceItem->setData(Qt::UserRole, level.price);
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        priceItem->setFont(mono);
        priceItem->setForeground(textColor);
        priceItem->setBackground(QBrush(rowColor));

        auto *qtyItem = new AnimatedBookItem(formatNumber(level.quantity));
        qtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        qtyItem->setForeground(textColor);
        qtyItem->setBackground(QBrush(rowColor));

        auto *ordersItem = new AnimatedBookItem(QString::number(level.orders));
        ordersItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ordersItem->setForeground(textColor);
        ordersItem->setBackground(QBrush(rowColor));

        auto *cumItem = new AnimatedBookItem(formatNumber(level.cumulativeQty));
        cumItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cumItem->setForeground(textColor);
        cumItem->setBackground(QBrush(rowColor));

        asksTable_->setItem(row, kPriceColumn, priceItem);
        asksTable_->setItem(row, kQtyColumn, qtyItem);
        asksTable_->setItem(row, kOrdersColumn, ordersItem);
        asksTable_->setItem(row, kCumQtyColumn, cumItem);

        const auto itPrevious = previousAskQtyByPrice_.find(level.price);
        if (itPrevious != previousAskQtyByPrice_.end() && itPrevious->second != level.quantity) {
            animateRowFlash(asksTable_, row, rowColor);
        }
    }

    previousAskQtyByPrice_ = std::move(currentQtyByPrice);
}

void OrderBookWidget::renderBids(const std::vector<PriceLevel> &bidLevels) {
    bidsTable_->setRowCount(static_cast<int>(bidLevels.size()));

    const QColor textColor("#a6e3a1");
    const QColor lightGreen("#4a6a53");
    const QColor deepGreen("#25382a");
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    std::map<double, uint64_t> currentQtyByPrice;

    for (int row = 0; row < static_cast<int>(bidLevels.size()); ++row) {
        const PriceLevel &level = bidLevels[static_cast<std::size_t>(row)];
        currentQtyByPrice[level.price] = level.quantity;

        const double t =
            (bidLevels.size() <= 1) ? 0.0 : static_cast<double>(row) / (bidLevels.size() - 1);
        const QColor rowColor = interpolateColor(lightGreen, deepGreen, t);

        auto *priceItem = new AnimatedBookItem(formatPrice(level.price));
        priceItem->setData(Qt::UserRole, level.price);
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        priceItem->setFont(mono);
        priceItem->setForeground(textColor);
        priceItem->setBackground(QBrush(rowColor));

        auto *qtyItem = new AnimatedBookItem(formatNumber(level.quantity));
        qtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        qtyItem->setForeground(textColor);
        qtyItem->setBackground(QBrush(rowColor));

        auto *ordersItem = new AnimatedBookItem(QString::number(level.orders));
        ordersItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ordersItem->setForeground(textColor);
        ordersItem->setBackground(QBrush(rowColor));

        auto *cumItem = new AnimatedBookItem(formatNumber(level.cumulativeQty));
        cumItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cumItem->setForeground(textColor);
        cumItem->setBackground(QBrush(rowColor));

        bidsTable_->setItem(row, kPriceColumn, priceItem);
        bidsTable_->setItem(row, kQtyColumn, qtyItem);
        bidsTable_->setItem(row, kOrdersColumn, ordersItem);
        bidsTable_->setItem(row, kCumQtyColumn, cumItem);

        const auto itPrevious = previousBidQtyByPrice_.find(level.price);
        if (itPrevious != previousBidQtyByPrice_.end() && itPrevious->second != level.quantity) {
            animateRowFlash(bidsTable_, row, rowColor);
        }
    }

    previousBidQtyByPrice_ = std::move(currentQtyByPrice);
}

void OrderBookWidget::updateSpreadLabel(const std::vector<PriceLevel> &bidLevels,
                                        const std::vector<PriceLevel> &askLevelsAscending) {
    if (bidLevels.empty() || askLevelsAscending.empty()) {
        spreadLabel_->setText("Spread: N/A");
        return;
    }

    const double bestBid = bidLevels.front().price;
    const double bestAsk = askLevelsAscending.front().price;
    const double spread = bestAsk - bestBid;
    spreadLabel_->setText(QString("Spread: $%1").arg(spread, 0, 'f', 2));
}

void OrderBookWidget::animateRowFlash(QTableWidget *table, int row, const QColor &targetColor) {
    for (int col = 0; col < table->columnCount(); ++col) {
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
