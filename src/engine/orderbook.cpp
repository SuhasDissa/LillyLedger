#include "engine/orderbook.h"
#include "engine/executionhandler.h"
#include "engine/matchingengine.h"
#include <algorithm>
#include <cstdio>

OrderBook::OrderBook(Instrument instrument) : instrument_(instrument) {}

void OrderBook::insertBuy(const BookEntry &entry) {

    auto it = std::lower_bound(buys_.begin(), buys_.end(), entry,
                               [](const BookEntry &a, const BookEntry &b) {
                                   if (a.order.price != b.order.price)
                                       return a.order.price > b.order.price;
                                   return a.seqNum < b.seqNum;
                               });
    buys_.insert(it, entry);
}

void OrderBook::insertSell(const BookEntry &entry) {

    auto it = std::lower_bound(sells_.begin(), sells_.end(), entry,
                               [](const BookEntry &a, const BookEntry &b) {
                                   if (a.order.price != b.order.price)
                                       return a.order.price < b.order.price;
                                   return a.seqNum < b.seqNum;
                               });
    sells_.insert(it, entry);
}

std::vector<ExecutionReport> OrderBook::addOrder(const Order &order, const char *orderId) {
    auto &opposite = (order.side == Side::Buy) ? sells_ : buys_;

    uint16_t remainingQty = order.quantity;

    std::vector<Trade> trades;
    if (MatchingEngine::isAggressive(order, opposite))
        trades = MatchingEngine::execute(order, opposite, remainingQty);

    std::vector<ExecutionReport> reports =
        ExecutionHandler::buildFillReports(order, orderId, trades);

    if (remainingQty > 0) {
        BookEntry entry{};
        entry.order = order;
        entry.remainingQty = remainingQty;
        entry.seqNum = nextSeqNum_++;
        std::snprintf(entry.orderId, sizeof(entry.orderId), "%s", orderId);

        if (order.side == Side::Buy)
            insertBuy(entry);
        else
            insertSell(entry);

        reports.push_back(ExecutionHandler::buildNewReport(entry));
    }

    return reports;
}
