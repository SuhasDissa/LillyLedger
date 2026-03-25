#pragma once
#include "core/order.h"
#include "engine/bookentry.h"
#include <vector>
#include <cstdint>

struct Trade {
    BookEntry passive;
    double    execPrice;
    uint16_t  execQty;
};


class MatchingEngine {
public:
    // Returns true when the incoming order can match at least one resting order
    // on the opposite side.
    // A Buy  order is aggressive when the best ask  (sells_.front().price) ≤ order.price.
    // A Sell order is aggressive when the best bid  (buys_.front().price)  ≥ order.price.
    static bool isAggressive(const Order               &order,
                              const std::vector<BookEntry> &opposite);


    static std::vector<Trade> execute(const Order            &order,
                                      std::vector<BookEntry> &opposite,
                                      uint16_t               &remainingQty);
};
