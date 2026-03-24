#include "engine/orderrouter.h"
#include <cstdio>

OrderRouter::OrderRouter()
    : books_{
          OrderBook{Instrument::Rose},
          OrderBook{Instrument::Lavender},
          OrderBook{Instrument::Lotus},
          OrderBook{Instrument::Tulip},
          OrderBook{Instrument::Orchid},
      } {}

std::vector<ExecutionReport> OrderRouter::route(const Order &order) {
    char orderId[8];
    std::snprintf(orderId, sizeof(orderId), "%u", nextOrderId_++);

    auto index = static_cast<std::size_t>(order.instrument);
    return books_[index].addOrder(order, orderId);
}
