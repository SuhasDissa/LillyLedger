#pragma once
#include "core/order.h"
#include "core/executionreport.h"
#include "engine/bookentry.h"
#include <vector>
#include <cstdint>

// Maintains the resting buy and sell orders for a single instrument.
// Matching is delegated to MatchingEngine; this class is responsible for
// sorted insertion, report construction, and book-keeping only.
class OrderBook {
public:
    explicit OrderBook(Instrument instrument);

    // Process an incoming order:
    //   • If it is aggressive, execute it against resting orders (price-time
    //     priority) and emit Fill / PFill reports for each match.
    //   • Any unmatched remainder (or the full order if passive) is placed on
    //     the book and a New report is emitted.
    std::vector<ExecutionReport> addOrder(const Order &order,
                                          const char  *orderId);

private:
    Instrument             instrument_;
    std::vector<BookEntry> buys_;  // sorted: highest price first, then lowest seqNum
    std::vector<BookEntry> sells_; // sorted: lowest  price first, then lowest seqNum
    uint32_t               nextSeqNum_{0};

    void insertBuy (const BookEntry &entry);
    void insertSell(const BookEntry &entry);

    // Build an execution report for a resting (passive) book entry.
    static ExecutionReport makePassiveReport(const BookEntry &entry,
                                             Status           status,
                                             double           execPrice,
                                             uint16_t         execQty);

    // Build an execution report for the incoming (aggressive) order.
    static ExecutionReport makeAggressorReport(const Order &order,
                                               const char  *orderId,
                                               Status       status,
                                               double       execPrice,
                                               uint16_t     execQty);
};
