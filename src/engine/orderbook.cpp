#include "engine/orderbook.h"
#include "utils/timestamp.h"
#include <algorithm>
#include <cstring>

OrderBook::OrderBook(Instrument instrument) : instrument_(instrument) {}

// ─── insertion helpers (keep vectors sorted) ─────────────────────────────────

void OrderBook::insertBuy(const BookEntry &entry) {
    // Descending price, ascending seqNum within the same price.
    auto it = std::lower_bound(
        buys_.begin(), buys_.end(), entry,
        [](const BookEntry &a, const BookEntry &b) {
            if (a.order.price != b.order.price)
                return a.order.price > b.order.price; // higher price first
            return a.seqNum < b.seqNum;               // earlier arrival first
        });
    buys_.insert(it, entry);
}

void OrderBook::insertSell(const BookEntry &entry) {
    // Ascending price, ascending seqNum within the same price.
    auto it = std::lower_bound(
        sells_.begin(), sells_.end(), entry,
        [](const BookEntry &a, const BookEntry &b) {
            if (a.order.price != b.order.price)
                return a.order.price < b.order.price; // lower price first
            return a.seqNum < b.seqNum;               // earlier arrival first
        });
    sells_.insert(it, entry);
}

// ─── report builders ──────────────────────────────────────────────────────────

ExecutionReport OrderBook::makeReport(const BookEntry &entry, Status status,
                                      double execPrice, uint16_t execQty) {
    ExecutionReport r{};
    std::snprintf(r.clientOrderId, sizeof(r.clientOrderId), "%s",
                  entry.order.clientOrderId);
    std::snprintf(r.orderId, sizeof(r.orderId), "%s", entry.orderId);
    r.instrument = entry.order.instrument;
    r.side       = entry.order.side;
    r.price      = execPrice;
    r.quantity   = execQty;
    r.status     = status;
    std::string ts = utils::currentTransactTime();
    std::snprintf(r.transactTime, sizeof(r.transactTime), "%s", ts.c_str());
    return r;
}

ExecutionReport OrderBook::makeAggressorReport(const Order &order,
                                               const char *orderId,
                                               Status status,
                                               double execPrice,
                                               uint16_t execQty) {
    ExecutionReport r{};
    std::snprintf(r.clientOrderId, sizeof(r.clientOrderId), "%s",
                  order.clientOrderId);
    std::snprintf(r.orderId, sizeof(r.orderId), "%s", orderId);
    r.instrument = order.instrument;
    r.side       = order.side;
    r.price      = execPrice;
    r.quantity   = execQty;
    r.status     = status;
    std::string ts = utils::currentTransactTime();
    std::snprintf(r.transactTime, sizeof(r.transactTime), "%s", ts.c_str());
    return r;
}

// ─── core matching logic ──────────────────────────────────────────────────────

std::vector<ExecutionReport> OrderBook::addOrder(const Order &order,
                                                 const char *orderId) {
    std::vector<ExecutionReport> reports;

    uint16_t remainingQty = order.quantity;
    uint32_t seqNum       = nextSeqNum_++;

    auto &opposite = (order.side == Side::Buy) ? sells_ : buys_;

    for (auto it = opposite.begin(); it != opposite.end() && remainingQty > 0;) {
        // Check price compatibility (passive order sets the execution price).
        bool canMatch = (order.side == Side::Buy)
                            ? (it->order.price <= order.price)
                            : (it->order.price >= order.price);
        if (!canMatch)
            break;

        uint16_t matchQty  = std::min(remainingQty, it->remainingQty);
        double   matchPrice = it->order.price;

        it->remainingQty -= matchQty;
        remainingQty     -= matchQty;

        // Passive side report.
        Status passiveStatus = (it->remainingQty == 0) ? Status::Fill
                                                        : Status::PFill;
        reports.push_back(makeReport(*it, passiveStatus, matchPrice, matchQty));

        // Aggressor side report.
        Status aggressorStatus = (remainingQty == 0) ? Status::Fill
                                                      : Status::PFill;
        reports.push_back(makeAggressorReport(order, orderId, aggressorStatus,
                                              matchPrice, matchQty));

        if (it->remainingQty == 0)
            it = opposite.erase(it);
        else
            ++it;
    }

    // Place any remainder (or the entire order if unmatched) on the book.
    if (remainingQty > 0) {
        BookEntry entry{};
        entry.order        = order;
        entry.remainingQty = remainingQty;
        entry.seqNum       = seqNum;
        std::snprintf(entry.orderId, sizeof(entry.orderId), "%s", orderId);

        if (order.side == Side::Buy)
            insertBuy(entry);
        else
            insertSell(entry);

        // New report uses the original order price and the remaining quantity.
        reports.push_back(makeReport(entry, Status::New, order.price,
                                     remainingQty));
    }

    return reports;
}
