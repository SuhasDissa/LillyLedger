#ifndef ORDER_H
#define ORDER_H

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include <cstdint>

struct Order {
    char clientOrderId[8]{};
    double price;
    uint16_t quantity;
    Instrument instrument;
    Side side;
};

#endif // ORDER_H