#include "engine/executionhandler.h"
#include "utils/timestamp.h"
#include <cstdio>
#include <cstring>

static ExecutionReport makeReport(const char *clientOrderId, const char *orderId,
                                  Instrument instrument, Side side, double price, uint16_t qty,
                                  Status status, const char *reason = "") {
    ExecutionReport r{};
    std::snprintf(r.clientOrderId, sizeof(r.clientOrderId), "%s", clientOrderId);
    std::snprintf(r.orderId, sizeof(r.orderId), "%s", orderId);
    r.instrument = instrument;
    r.side = side;
    r.price = price;
    r.quantity = qty;
    r.status = status;
    std::snprintf(r.reason, sizeof(r.reason), "%s", reason);
    std::string ts = utils::currentTransactTime();
    std::snprintf(r.transactTime, sizeof(r.transactTime), "%s", ts.c_str());
    return r;
}

std::vector<ExecutionReport> ExecutionHandler::buildFillReports(const Order &aggressor,
                                                                const char *aggressorOrderId,
                                                                const std::vector<Trade> &trades) {

    std::vector<ExecutionReport> reports;
    reports.reserve(trades.size() * 2);

    uint16_t aggressorFilled = 0;

    for (const Trade &t : trades) {

        const double execPrice = t.execPrice;

        aggressorFilled += t.execQty;

        bool passiveFull = (t.passive.remainingQty == t.execQty);
        Status passiveStatus = passiveFull ? Status::Fill : Status::PFill;

        Status aggressorStatus =
            (aggressorFilled == aggressor.quantity) ? Status::Fill : Status::PFill;

        reports.push_back(makeReport(t.passive.order.clientOrderId, t.passive.orderId,
                                     t.passive.order.instrument, t.passive.order.side, execPrice,
                                     t.execQty, passiveStatus));

        reports.push_back(makeReport(aggressor.clientOrderId, aggressorOrderId,
                                     aggressor.instrument, aggressor.side, execPrice, t.execQty,
                                     aggressorStatus));
    }

    return reports;
}

ExecutionReport ExecutionHandler::buildNewReport(const BookEntry &entry) {
    return makeReport(entry.order.clientOrderId, entry.orderId, entry.order.instrument,
                      entry.order.side, entry.order.price, entry.remainingQty, Status::New);
}

ExecutionReport ExecutionHandler::buildRejectedReport(const char *clientOrderId,
                                                      const std::string &reason) {
    ExecutionReport r{};
    std::snprintf(r.clientOrderId, sizeof(r.clientOrderId), "%s", clientOrderId);
    r.status = Status::Rejected;
    std::snprintf(r.reason, sizeof(r.reason), "%s", reason.c_str());
    std::string ts = utils::currentTransactTime();
    std::snprintf(r.transactTime, sizeof(r.transactTime), "%s", ts.c_str());
    return r;
}
