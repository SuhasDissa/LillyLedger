#include "engine/orderbook.h"
#include "engine/matchingengine.h"
#include "utils/timestamp.h"
#include <algorithm>
#include <cstring>

OrderBook::OrderBook(Instrument instrument) : instrument_(instrument) {}

// ─── sorted insertion ─────────────────────────────────────────────────────────

void OrderBook::insertBuy(const BookEntry &entry) {
    // Descending price; ascending seqNum within the same price level.
    auto it = std::lower_bound(
        buys_.begin(), buys_.end(), entry,
        [](const BookEntry &a, const BookEntry &b) {
            if (a.order.price != b.order.price)
                return a.order.price > b.order.price;
            return a.seqNum < b.seqNum;
        });
    buys_.insert(it, entry);
}

void OrderBook::insertSell(const BookEntry &entry) {
    // Ascending price; ascending seqNum within the same price level.
    auto it = std::lower_bound(
        sells_.begin(), sells_.end(), entry,
        [](const BookEntry &a, const BookEntry &b) {
            if (a.order.price != b.order.price)
                return a.order.price < b.order.price;
            return a.seqNum < b.seqNum;
        });
    sells_.insert(it, entry);
}

// ─── report builders ──────────────────────────────────────────────────────────

ExecutionReport OrderBook::makePassiveReport(const BookEntry &entry,
                                             Status           status,
                                             double           execPrice,
                                             uint16_t         execQty) {
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
                                               const char  *orderId,
                                               Status       status,
                                               double       execPrice,
                                               uint16_t     execQty) {
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

// ─── addOrder ─────────────────────────────────────────────────────────────────

std::vector<ExecutionReport> OrderBook::addOrder(const Order &order,
                                                 const char  *orderId) {
    std::vector<ExecutionReport> reports;

    auto &opposite = (order.side == Side::Buy) ? sells_ : buys_;

    // ── Step 1: classify the order ────────────────────────────────────────────
    //
    // Aggressive: the order crosses the spread — at least one resting order on
    //             the opposite side has a compatible price.
    // Passive:    no resting order can be matched; the order enters the book.

    uint16_t remainingQty = order.quantity;

    if (MatchingEngine::isAggressive(order, opposite)) {
        // ── Step 2 (aggressive path): execute all available matches ───────────
        std::vector<Trade> trades =
            MatchingEngine::execute(order, opposite, remainingQty);

        for (const Trade &t : trades) {
            // The passive entry's final status depends on whether it still has
            // quantity left after this match (the Trade snapshot captures the
            // state *before* the decrement, so check remainingQty in snapshot).
            bool passiveFullyFilled = (t.passive.remainingQty == t.execQty);
            Status passiveStatus =
                passiveFullyFilled ? Status::Fill : Status::PFill;

            // Whether this specific fill completes the aggressor.
            Status aggressorStatus =
                (remainingQty == 0 && &t == &trades.back())
                    ? Status::Fill
                    : Status::PFill;

            reports.push_back(
                makePassiveReport(t.passive, passiveStatus, t.execPrice, t.execQty));
            reports.push_back(
                makeAggressorReport(order, orderId, aggressorStatus, t.execPrice, t.execQty));
        }
    }

    // ── Step 3: rest any unmatched remainder ──────────────────────────────────
    //
    // This covers:
    //   • A fully passive order (no matches at all).
    //   • The remainder of a partially aggressive order.
    if (remainingQty > 0) {
        BookEntry entry{};
        entry.order        = order;
        entry.remainingQty = remainingQty;
        entry.seqNum       = nextSeqNum_++;
        std::snprintf(entry.orderId, sizeof(entry.orderId), "%s", orderId);

        if (order.side == Side::Buy)
            insertBuy(entry);
        else
            insertSell(entry);

        reports.push_back(
            makePassiveReport(entry, Status::New, order.price, remainingQty));
    }

    return reports;
}
