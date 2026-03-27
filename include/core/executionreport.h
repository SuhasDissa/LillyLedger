#pragma once
#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"
#include "core/order.h"
#include <cstdint>

inline constexpr std::size_t kOrderIdLen = 8;
inline constexpr std::size_t kReasonLen = 51;
inline constexpr std::size_t kTransactTimeLen = 20;

struct ExecutionReport {
    char clientOrderId[kClientOrderIdLen]{};
    char orderId[kOrderIdLen]{};
    Instrument instrument;
    Side side;
    double price;
    uint16_t quantity;
    Status status;
    char reason[kReasonLen]{};
    char transactTime[kTransactTimeLen]{}; // YYYYMMDD-HHMMSS.sss
};