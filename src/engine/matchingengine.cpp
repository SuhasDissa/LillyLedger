#include "engine/matchingengine.h"
#include <algorithm>

static bool pricesCross(const Order &aggressor, const BookEntry &passive) {
    if (aggressor.side == Side::Buy)
        return passive.order.price <= aggressor.price;
    else
        return passive.order.price >= aggressor.price;
}

bool MatchingEngine::isAggressive(const Order &order, const std::vector<BookEntry> &opposite) {

    if (opposite.empty())
        return false;
    return pricesCross(order, opposite.front());
}

std::vector<Trade> MatchingEngine::execute(const Order &order, std::vector<BookEntry> &opposite,
                                           uint16_t &remainingQty) {
    std::vector<Trade> trades;

    for (auto it = opposite.begin(); it != opposite.end() && remainingQty > 0;) {
        if (!pricesCross(order, *it))
            break;

        uint16_t matchQty = std::min(remainingQty, it->remainingQty);

        Trade trade;
        trade.passive = *it;
        trade.execPrice = it->order.price;
        trade.execQty = matchQty;
        trades.push_back(trade);

        remainingQty -= matchQty;
        it->remainingQty -= matchQty;

        if (it->remainingQty == 0)
            it = opposite.erase(it);
        else
            ++it;
    }

    return trades;
}
