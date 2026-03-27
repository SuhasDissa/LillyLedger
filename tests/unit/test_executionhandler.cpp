#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include "engine/executionhandler.h"
#include "engine/matchingengine.h"
#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"

// ─── helpers ──────────────────────────────────────────────────────────────────

static Order makeOrder(const char *cid, double price, uint16_t qty, Side side,
                       Instrument instr = Instrument::Rose) {
    Order o{};
    std::snprintf(o.clientOrderId, sizeof(o.clientOrderId), "%s", cid);
    o.price      = price;
    o.quantity   = qty;
    o.side       = side;
    o.instrument = instr;
    return o;
}

static BookEntry makeEntry(const char *cid, const char *oid, double price,
                           uint16_t qty, Side side, uint32_t seq = 0,
                           Instrument instr = Instrument::Rose) {
    BookEntry e{};
    std::snprintf(e.order.clientOrderId, sizeof(e.order.clientOrderId), "%s", cid);
    e.order.price      = price;
    e.order.quantity   = qty;
    e.order.side       = side;
    e.order.instrument = instr;
    std::snprintf(e.orderId, sizeof(e.orderId), "%s", oid);
    e.remainingQty = qty;
    e.seqNum       = seq;
    return e;
}

// Returns true if the timestamp string looks like YYYYMMDD-HHMMSS.sss
static bool isValidTimestamp(const char *ts) {
    if (std::strlen(ts) != 19) return false;
    return ts[8] == '-' && ts[15] == '.';
}

// ─── buildNewReport ───────────────────────────────────────────────────────────

TEST(ExecutionHandlerBuildNewReport, FieldsMatchBookEntry) {
    BookEntry entry = makeEntry("cli1", "42", 100.0, 50, Side::Buy, 0, Instrument::Tulip);

    ExecutionReport r = ExecutionHandler::buildNewReport(entry);

    EXPECT_STREQ(r.clientOrderId, "cli1");
    EXPECT_STREQ(r.orderId,       "42");
    EXPECT_EQ(r.instrument,       Instrument::Tulip);
    EXPECT_EQ(r.side,             Side::Buy);
    EXPECT_DOUBLE_EQ(r.price,     100.0);
    EXPECT_EQ(r.quantity,         50);
    EXPECT_EQ(r.status,           Status::New);
    EXPECT_STREQ(r.reason,        "");
    EXPECT_TRUE(isValidTimestamp(r.transactTime));
}

TEST(ExecutionHandlerBuildNewReport, ReportsRemainingQtyNotOriginalQty) {
    BookEntry entry = makeEntry("cli1", "1", 100.0, 40, Side::Sell);
    entry.remainingQty = 20; // partial remainder after prior fills

    ExecutionReport r = ExecutionHandler::buildNewReport(entry);

    EXPECT_EQ(r.quantity, 20);
}

// ─── buildRejectedReport ─────────────────────────────────────────────────────

TEST(ExecutionHandlerBuildRejectedReport, FieldsAreCorrect) {
    ExecutionReport r = ExecutionHandler::buildRejectedReport("badid", "invalid quantity");

    EXPECT_STREQ(r.clientOrderId, "badid");
    EXPECT_EQ(r.status,           Status::Rejected);
    EXPECT_STREQ(r.reason,        "invalid quantity");
    EXPECT_TRUE(isValidTimestamp(r.transactTime));
}

TEST(ExecutionHandlerBuildRejectedReport, OrderIdIsEmpty) {
    ExecutionReport r = ExecutionHandler::buildRejectedReport("c1", "reason");
    EXPECT_STREQ(r.orderId, "");
}

TEST(ExecutionHandlerBuildRejectedReport, LongReasonIsTruncatedTo50Chars) {
    std::string longReason(60, 'x');
    ExecutionReport r = ExecutionHandler::buildRejectedReport("c1", longReason);

    // reason field is char[51], snprintf writes at most 50 chars + null
    EXPECT_EQ(std::strlen(r.reason), 50u);
}

// ─── buildFillReports ─────────────────────────────────────────────────────────

TEST(ExecutionHandlerBuildFillReports, EmptyTradesReturnsEmptyVector) {
    Order aggressor = makeOrder("agg", 100.0, 10, Side::Buy);
    std::vector<Trade> trades;

    auto reports = ExecutionHandler::buildFillReports(aggressor, "99", trades);

    EXPECT_TRUE(reports.empty());
}

TEST(ExecutionHandlerBuildFillReports, SingleFullFillProducesTwoReports) {
    Order aggressor = makeOrder("agg", 100.0, 10, Side::Buy);
    BookEntry passive = makeEntry("pas", "1", 100.0, 10, Side::Sell);

    Trade t;
    t.passive   = passive;
    t.execPrice = 100.0;
    t.execQty   = 10;

    auto reports = ExecutionHandler::buildFillReports(aggressor, "2", {t});

    ASSERT_EQ(reports.size(), 2u);

    // Passive report (index 0)
    EXPECT_STREQ(reports[0].clientOrderId, "pas");
    EXPECT_STREQ(reports[0].orderId,       "1");
    EXPECT_EQ(reports[0].status,           Status::Fill);
    EXPECT_EQ(reports[0].quantity,         10);
    EXPECT_DOUBLE_EQ(reports[0].price,     100.0);

    // Aggressor report (index 1)
    EXPECT_STREQ(reports[1].clientOrderId, "agg");
    EXPECT_STREQ(reports[1].orderId,       "2");
    EXPECT_EQ(reports[1].status,           Status::Fill);
    EXPECT_EQ(reports[1].quantity,         10);
    EXPECT_DOUBLE_EQ(reports[1].price,     100.0);
}

TEST(ExecutionHandlerBuildFillReports, PassivePartialFillGetsPFillStatus) {
    // Passive has 20 qty, only 10 executes → PFill for passive
    Order aggressor = makeOrder("agg", 100.0, 10, Side::Buy);
    BookEntry passive = makeEntry("pas", "1", 100.0, 20, Side::Sell);

    Trade t;
    t.passive   = passive;
    t.execPrice = 100.0;
    t.execQty   = 10;

    auto reports = ExecutionHandler::buildFillReports(aggressor, "2", {t});

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::PFill); // passive not fully consumed
    EXPECT_EQ(reports[1].status, Status::Fill);  // aggressor fully consumed (10/10)
}

TEST(ExecutionHandlerBuildFillReports, AggressorPartialFillGetsPFillStatus) {
    // Aggressor wants 20, passive only has 10 → PFill for aggressor
    Order aggressor = makeOrder("agg", 100.0, 20, Side::Buy);
    BookEntry passive = makeEntry("pas", "1", 100.0, 10, Side::Sell);

    Trade t;
    t.passive   = passive;
    t.execPrice = 100.0;
    t.execQty   = 10;

    auto reports = ExecutionHandler::buildFillReports(aggressor, "2", {t});

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::Fill);  // passive fully consumed
    EXPECT_EQ(reports[1].status, Status::PFill); // aggressor only partially consumed
}

TEST(ExecutionHandlerBuildFillReports, MultipleTradesAggressorGotsFillOnLastTrade) {
    // Aggressor (30 qty) fills 3 passives of 10 each
    Order aggressor = makeOrder("agg", 100.0, 30, Side::Buy);

    auto makePassiveTrade = [](const char *cid, const char *oid) {
        BookEntry e = {};
        std::snprintf(e.order.clientOrderId, sizeof(e.order.clientOrderId), "%s", cid);
        e.order.price      = 100.0;
        e.order.quantity   = 10;
        e.order.side       = Side::Sell;
        e.order.instrument = Instrument::Rose;
        std::snprintf(e.orderId, sizeof(e.orderId), "%s", oid);
        e.remainingQty = 10;
        Trade t;
        t.passive   = e;
        t.execPrice = 100.0;
        t.execQty   = 10;
        return t;
    };

    std::vector<Trade> trades = {
        makePassiveTrade("p1", "1"),
        makePassiveTrade("p2", "2"),
        makePassiveTrade("p3", "3"),
    };

    auto reports = ExecutionHandler::buildFillReports(aggressor, "99", trades);

    ASSERT_EQ(reports.size(), 6u);

    // First two trades: aggressor gets PFill
    EXPECT_EQ(reports[1].status, Status::PFill); // after trade 1: 10/30 filled
    EXPECT_EQ(reports[3].status, Status::PFill); // after trade 2: 20/30 filled
    // Last trade: aggressor gets Fill
    EXPECT_EQ(reports[5].status, Status::Fill);  // after trade 3: 30/30 filled
}

TEST(ExecutionHandlerBuildFillReports, ReportsUsePassivePriceNotAggressorPrice) {
    // Aggressive buy at 110, passive sell at 90 → exec at 90 (price improvement)
    Order aggressor = makeOrder("agg", 110.0, 10, Side::Buy);
    BookEntry passive = makeEntry("pas", "1", 90.0, 10, Side::Sell);

    Trade t;
    t.passive   = passive;
    t.execPrice = 90.0; // set by execute()
    t.execQty   = 10;

    auto reports = ExecutionHandler::buildFillReports(aggressor, "2", {t});

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_DOUBLE_EQ(reports[0].price, 90.0);
    EXPECT_DOUBLE_EQ(reports[1].price, 90.0);
}

TEST(ExecutionHandlerBuildFillReports, InstrumentAndSidePreservedPerSide) {
    Order aggressor = makeOrder("agg", 100.0, 10, Side::Buy, Instrument::Orchid);
    BookEntry passive = makeEntry("pas", "1", 100.0, 10, Side::Sell, 0, Instrument::Orchid);

    Trade t;
    t.passive   = passive;
    t.execPrice = 100.0;
    t.execQty   = 10;

    auto reports = ExecutionHandler::buildFillReports(aggressor, "2", {t});

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].instrument, Instrument::Orchid);
    EXPECT_EQ(reports[0].side,       Side::Sell);
    EXPECT_EQ(reports[1].instrument, Instrument::Orchid);
    EXPECT_EQ(reports[1].side,       Side::Buy);
}
