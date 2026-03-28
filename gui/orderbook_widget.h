#pragma once

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "engine/bookentry.h"
#include <QWidget>
#include <map>
#include <vector>

class QLabel;
class QSplitter;
class QTableWidget;

class OrderBookWidget : public QWidget {
    Q_OBJECT

  public:
    explicit OrderBookWidget(QWidget *parent = nullptr);

  public slots:
    void updateBook(const std::vector<BookEntry> &buys, const std::vector<BookEntry> &sells,
                    Instrument instrument);

  signals:
    void priceClicked(double price, Side side);

  private:
    struct PriceLevel {
        double price{0.0};
        uint64_t quantity{0};
        int orders{0};
        uint64_t cumulativeQty{0};
    };

    void setupUi();
    static void setupLadderTable(QTableWidget *table);
    static QString instrumentLabelText(Instrument instrument);
    static QColor interpolateColor(const QColor &from, const QColor &to, double t);
    static std::vector<PriceLevel> buildBuyLevels(const std::vector<BookEntry> &buys);
    static std::vector<PriceLevel> buildSellLevelsAscending(const std::vector<BookEntry> &sells);
    void renderAsks(const std::vector<PriceLevel> &askLevelsAscending);
    void renderBids(const std::vector<PriceLevel> &bidLevels);
    void updateSpreadLabel(const std::vector<PriceLevel> &bidLevels,
                           const std::vector<PriceLevel> &askLevelsAscending);
    static void animateRowFlash(QTableWidget *table, int row, const QColor &targetColor);
    static QString formatPrice(double price);
    static QString formatNumber(uint64_t value);

    QLabel *instrumentHeaderLabel_{nullptr};
    QLabel *spreadLabel_{nullptr};
    QSplitter *ladderSplitter_{nullptr};
    QTableWidget *asksTable_{nullptr};
    QTableWidget *bidsTable_{nullptr};

    std::map<double, uint64_t> previousAskQtyByPrice_;
    std::map<double, uint64_t> previousBidQtyByPrice_;
};
