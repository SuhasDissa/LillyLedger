#pragma once
#include "core/executionreport.h"
#include "core/order.h"
#include "engine/bookentry.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

// Maintains the resting buy and sell orders for a single instrument.
class OrderBook {
  public:
    explicit OrderBook(Instrument instrument);

    // Process an incoming order.  Returns all execution reports generated.
    std::vector<ExecutionReport> addOrder(const Order &order, const char *orderId);

  private:
    Instrument instrument_;
    // buys: highest price first
    std::map<double, std::deque<BookEntry>, std::greater<double>> buys_;
    // sells: lowest price first
    std::map<double, std::deque<BookEntry>> sells_;
    uint32_t nextSeqNum_{0};

    void insertBuy(const BookEntry &entry);
    void insertSell(const BookEntry &entry);
};
