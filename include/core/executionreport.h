#ifndef EXECUTIONREPORT_H
#define EXECUTIONREPORT_H

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"
#include <cstdint>

struct ExecutionReport {
    char clientOrderId[8]{};
    char orderId[8]{};
    Instrument instrument;
    Side side;
    double price;
    uint16_t quantity;
    Status status;
    char reason[51]{};
    char transactTime[20]{}; // YYYYMMDD-HHMMSS.sss
};

#endif // EXECUTIONREPORT_H