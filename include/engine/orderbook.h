#pragma once
#include "core/order.h"
#include "core/executionreport.h"
#include <vector>
#include <cstdint>

// Manages the buy and sell sides of a single instrument using price-time
// priority matching.  Buy orders are matched against the lowest available ask;
// sell orders are matched against the highest available bid.
class OrderBook {
public:
    explicit OrderBook(Instrument instrument);

    // Attempt to match the order against resting orders, then place any
    // unmatched remainder on the book.  Returns all execution reports generated
    // by this operation (one per side per individual match, plus a New report
    // if the order or its remainder enters the book).
    std::vector<ExecutionReport> addOrder(const Order &order,
                                          const char *orderId);

private:
    struct BookEntry {
        Order order;
        char orderId[8];
        uint16_t remainingQty;
        uint32_t seqNum; // arrival sequence — lower = earlier
    };

    Instrument instrument_;
    std::vector<BookEntry> buys_;  // highest price first, then lowest seqNum
    std::vector<BookEntry> sells_; // lowest price first, then lowest seqNum
    uint32_t nextSeqNum_{0};

    void insertBuy(const BookEntry &entry);
    void insertSell(const BookEntry &entry);

    static ExecutionReport makeReport(const BookEntry &entry, Status status,
                                      double execPrice, uint16_t execQty);
    static ExecutionReport makeAggressorReport(const Order &order,
                                               const char *orderId,
                                               Status status,
                                               double execPrice,
                                               uint16_t execQty);
};
