#pragma once
#include "core/order.h"
#include "core/executionreport.h"
#include "engine/orderbook.h"
#include <array>
#include <vector>
#include <cstdint>

// Owns one OrderBook per instrument and dispatches incoming orders to the
// correct book.  Assigns a unique exchange-side orderId to each order.
class OrderRouter {
public:
    OrderRouter();

    // Route a valid order to its instrument's order book.
    // Returns all execution reports produced by that operation.
    std::vector<ExecutionReport> route(const Order &order);

private:
    // Indexed by the underlying value of Instrument (Rose=0 … Orchid=4).
    std::array<OrderBook, 5> books_;
    uint32_t nextOrderId_{1};
};
