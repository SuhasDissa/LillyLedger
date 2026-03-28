#pragma once

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "engine/bookentry.h"
#include <QString>
#include <QWidget>
#include <map>
#include <vector>

class QLabel;
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
    struct BookRow {
        int prioritySeq{0};
        QString orderId;
        QString traderId;
        uint64_t quantity{0};
        double price{0.0};
    };

    void setupUi();
    static void setupBookTable(QTableWidget *table);
    static QString instrumentLabelText(Instrument instrument);
    static std::vector<BookRow> buildBookRows(const std::vector<BookEntry> &entries);
    void renderBookTable(const std::vector<BookRow> &bidRows, const std::vector<BookRow> &askRows);
    void updateSpreadLabel(const std::vector<BookRow> &bidRows, const std::vector<BookRow> &askRows);
    static void animateCellsFlash(QTableWidget *table, int row, int firstColumn, int lastColumn,
                                  const QColor &targetColor);
    static QString formatPrice(double price);
    static QString formatNumber(uint64_t value);

    QLabel *instrumentHeaderLabel_{nullptr};
    QLabel *spreadLabel_{nullptr};
    QTableWidget *bookTable_{nullptr};

    std::map<QString, uint64_t> previousAskQtyByOrderId_;
    std::map<QString, uint64_t> previousBidQtyByOrderId_;
};
