#pragma once
#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t kClientOrderIdLen = 8;

struct Order {
    char clientOrderId[kClientOrderIdLen]{};
    double price;
    uint16_t quantity;
    Instrument instrument;
    Side side;
};
