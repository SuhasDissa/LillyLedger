#pragma once
#include "core/order.h"
#include <cstdint>

// A resting order in the book — wraps the original Order with the exchange-
// assigned orderId, the quantity still available to trade, and an arrival
// sequence number used to break price ties (FIFO within a price level).
struct BookEntry {
    Order order;
    char orderId[8];
    uint16_t remainingQty;
    uint32_t seqNum; // lower = earlier
};
