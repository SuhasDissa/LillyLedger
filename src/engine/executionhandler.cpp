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

    // Track how much of the aggressor has been filled so far to determine
    // whether each execution event produces a Fill or a PFill for that side.
    uint16_t aggressorFilled = 0;

    for (const Trade &t : trades) {
        // ── Execution price ───────────────────────────────────────────────────
        // The passive order always sets the price.  An aggressive buy at $11
        // against a resting sell at $10 executes at $10 (price improvement).
        const double execPrice = t.execPrice; // == passive.order.price

        aggressorFilled += t.execQty;

        // ── Passive-side status ───────────────────────────────────────────────
        // Trade snapshots passive.remainingQty before the decrement.
        bool passiveFull = (t.passive.remainingQty == t.execQty);
        Status passiveStatus = passiveFull ? Status::Fill : Status::PFill;

        // ── Aggressor-side status ─────────────────────────────────────────────
        // Fill only when the aggressor's cumulative fills reach its full qty.
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
