#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include "engine/orderrouter.h"
#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"

// ─── helpers ──────────────────────────────────────────────────────────────────

static Order makeOrder(const char *cid, double price, uint16_t qty, Side side,
                       Instrument instr) {
    Order o{};
    std::snprintf(o.clientOrderId, sizeof(o.clientOrderId), "%s", cid);
    o.price      = price;
    o.quantity   = qty;
    o.side       = side;
    o.instrument = instr;
    return o;
}

// ─── order ID assignment ──────────────────────────────────────────────────────

TEST(OrderRouterRoute, FirstOrderGetsId1) {
    OrderRouter router;
    auto reports = router.route(makeOrder("c1", 100.0, 10, Side::Buy, Instrument::Rose));

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_STREQ(reports[0].orderId, "1");
}

TEST(OrderRouterRoute, SequentialOrdersGetIncrementalIds) {
    OrderRouter router;
    auto r1 = router.route(makeOrder("c1", 100.0, 10, Side::Buy,  Instrument::Rose));
    auto r2 = router.route(makeOrder("c2", 200.0, 10, Side::Sell, Instrument::Tulip));
    auto r3 = router.route(makeOrder("c3", 150.0, 10, Side::Buy,  Instrument::Orchid));

    ASSERT_EQ(r1.size(), 1u);
    ASSERT_EQ(r2.size(), 1u);
    ASSERT_EQ(r3.size(), 1u);

    EXPECT_STREQ(r1[0].orderId, "1");
    EXPECT_STREQ(r2[0].orderId, "2");
    EXPECT_STREQ(r3[0].orderId, "3");
}

// ─── instrument routing ───────────────────────────────────────────────────────

TEST(OrderRouterRoute, OrdersForDifferentInstrumentsDoNotCrossMatch) {
    OrderRouter router;
    // Rose sell at 100
    router.route(makeOrder("s1", 100.0, 10, Side::Sell, Instrument::Rose));
    // Tulip buy at 100 — should NOT match the Rose sell
    auto reports = router.route(makeOrder("b1", 100.0, 10, Side::Buy, Instrument::Tulip));

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].status, Status::New);
}

TEST(OrderRouterRoute, MatchOccursOnlyWithinSameInstrument) {
    OrderRouter router;
    // Lavender sell
    router.route(makeOrder("s1", 100.0, 10, Side::Sell, Instrument::Lavender));
    // Lavender buy at same price → should match
    auto reports = router.route(makeOrder("b1", 100.0, 10, Side::Buy, Instrument::Lavender));

    // 2 fill reports, no New
    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::Fill);
    EXPECT_EQ(reports[1].status, Status::Fill);
}

TEST(OrderRouterRoute, AllFiveInstrumentsHaveIndependentBooks) {
    OrderRouter router;
    Instrument instruments[] = {
        Instrument::Rose, Instrument::Lavender, Instrument::Lotus,
        Instrument::Tulip, Instrument::Orchid
    };

    // Add a passive sell for each instrument
    for (auto instr : instruments)
        router.route(makeOrder("seller", 100.0, 10, Side::Sell, instr));

    // A buy for one instrument only matches that instrument's book
    auto reports = router.route(makeOrder("buyer", 100.0, 10, Side::Buy, Instrument::Lotus));

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::Fill);
    EXPECT_EQ(reports[0].instrument, Instrument::Lotus);
}

// ─── passive order handling ───────────────────────────────────────────────────

TEST(OrderRouterRoute, PassiveOrderReturnsNewReportWithCorrectFields) {
    OrderRouter router;
    auto reports = router.route(makeOrder("myid", 99.5, 20, Side::Sell, Instrument::Orchid));

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].status,     Status::New);
    EXPECT_STREQ(reports[0].clientOrderId, "myid");
    EXPECT_EQ(reports[0].instrument, Instrument::Orchid);
    EXPECT_EQ(reports[0].side,       Side::Sell);
    EXPECT_DOUBLE_EQ(reports[0].price, 99.5);
    EXPECT_EQ(reports[0].quantity,   20);
}

// ─── order ID counter is shared across instruments ────────────────────────────

TEST(OrderRouterRoute, OrderIdCounterIsGlobalNotPerInstrument) {
    OrderRouter router;
    // Send one order per instrument to advance the counter
    router.route(makeOrder("c1", 100.0, 10, Side::Buy, Instrument::Rose));
    router.route(makeOrder("c2", 100.0, 10, Side::Buy, Instrument::Lavender));

    // Third order (any instrument) should get ID 3
    auto reports = router.route(makeOrder("c3", 100.0, 10, Side::Buy, Instrument::Lotus));
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_STREQ(reports[0].orderId, "3");
}
