#pragma once
#include "core/executionreport.h"
#include "core/order.h"
#include "engine/bookentry.h"
#include "engine/matchingengine.h"
#include <string>
#include <vector>

// Converts raw matching results (Trade objects) and book events into the
// ExecutionReport structs that are ultimately written to the output CSV.
//
// All report-construction logic lives here so that OrderBook and the entry
// point (main) stay free of field-level formatting details.
//
// Execution price rules
// ──────────────────────
// In price-time priority matching the passive (resting) order always sets the
// execution price.  An aggressive order that bids $11 against a $10 ask is
// filled at $10 — the buyer gets price improvement.  This is modelled by
// storing execPrice = passive.order.price inside each Trade.
class ExecutionHandler {
  public:
    // Build one pair of execution reports (passive + aggressor) per Trade.
    //
    // Status determination
    // ─────────────────────
    // Passive  : Fill  when trade.execQty == trade.passive.remainingQty
    //            PFill when a remainder stays in the book
    // Aggressor: Fill  when the cumulative filled quantity reaches order.quantity
    //            PFill until that point
    //
    // The Trade snapshot captures passive.remainingQty *before* the match,
    // so the comparison is exact even when the same passive entry is partially
    // consumed across multiple aggressive orders.
    static std::vector<ExecutionReport> buildFillReports(const Order &aggressor,
                                                         const char *aggressorOrderId,
                                                         const std::vector<Trade> &trades);

    // Build a New report for an order (or its remainder) that has been placed
    // on the book.  Price and quantity reflect what is actually resting.
    static ExecutionReport buildNewReport(const BookEntry &entry);

    // Build a Rejected report for an order that failed validation.
    // clientOrderId is copied from ParseResult.order.clientOrderId (best-effort
    // truncation of the raw input value is done by CSVReader).
    static ExecutionReport buildRejectedReport(const char *clientOrderId,
                                               const std::string &reason);
};
