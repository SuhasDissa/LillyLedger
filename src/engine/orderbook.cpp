#include "engine/orderbook.h"
#include "engine/executionhandler.h"
#include "engine/matchingengine.h" // for Trade struct
#include <algorithm>
#include <cstdio>

OrderBook::OrderBook(Instrument instrument) : instrument_(instrument) {}

void OrderBook::insertBuy(const BookEntry &entry) {
    buys_[entry.order.price].push_back(entry);
}

void OrderBook::insertSell(const BookEntry &entry) {
    sells_[entry.order.price].push_back(entry);
}

template <typename Map>
static std::vector<Trade> matchAgainst(const Order &aggressor, Map &opposite,
                                       uint16_t &remainingQty) {
    std::vector<Trade> trades;

    for (auto levelIt = opposite.begin(); levelIt != opposite.end() && remainingQty > 0;) {
        const double levelPrice = levelIt->first;
        const bool crosses = (aggressor.side == Side::Buy) ? (levelPrice <= aggressor.price)
                                                           : (levelPrice >= aggressor.price);
        if (!crosses)
            break;

        auto &queue = levelIt->second;
        while (!queue.empty() && remainingQty > 0) {
            BookEntry &passive = queue.front();
            const uint16_t matchQty = std::min(remainingQty, passive.remainingQty);

            Trade trade;
            trade.passive = passive; // snapshot before decrement
            trade.execPrice = levelPrice;
            trade.execQty = matchQty;
            trades.push_back(trade);

            remainingQty -= matchQty;
            passive.remainingQty -= matchQty;

            if (passive.remainingQty == 0)
                queue.pop_front(); // O(1) front removal
        }

        if (queue.empty())
            levelIt = opposite.erase(levelIt);
        else
            ++levelIt;
    }

    return trades;
}

std::vector<ExecutionReport> OrderBook::addOrder(const Order &order, const char *orderId) {
    uint16_t remainingQty = order.quantity;
    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        if (!sells_.empty() && sells_.begin()->first <= order.price)
            trades = matchAgainst(order, sells_, remainingQty);
    } else {
        if (!buys_.empty() && buys_.begin()->first >= order.price)
            trades = matchAgainst(order, buys_, remainingQty);
    }

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
