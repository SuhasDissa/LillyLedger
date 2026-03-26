#pragma once
#include "core/order.h"
#include "core/executionreport.h"
#include "engine/bookentry.h"
#include <vector>
#include <cstdint>

// Maintains the resting buy and sell orders for a single instrument.
// Matching is delegated to MatchingEngine; report construction is delegated
// to ExecutionHandler.  This class is responsible only for sorted insertion
// and book-keeping.
class OrderBook {
public:
    explicit OrderBook(Instrument instrument);

    // Process an incoming order.  Returns all execution reports generated.
    std::vector<ExecutionReport> addOrder(const Order &order,
                                          const char  *orderId);

private:
    Instrument             instrument_;
    std::vector<BookEntry> buys_;  // highest price first, then lowest seqNum
    std::vector<BookEntry> sells_; // lowest  price first, then lowest seqNum
    uint32_t               nextSeqNum_{0};

    void insertBuy (const BookEntry &entry);
    void insertSell(const BookEntry &entry);
};
