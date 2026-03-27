#include <gtest/gtest.h>
#include <cstdio>
#include "engine/orderbook.h"
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

// ─── passive (non-aggressive) orders ─────────────────────────────────────────

TEST(OrderBookAddOrder, PassiveBuyRestsAndReturnsNewReport) {
    OrderBook book(Instrument::Rose);
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);

    auto reports = book.addOrder(buy, "1");

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].status,   Status::New);
    EXPECT_STREQ(reports[0].clientOrderId, "c1");
    EXPECT_STREQ(reports[0].orderId,       "1");
    EXPECT_DOUBLE_EQ(reports[0].price, 100.0);
    EXPECT_EQ(reports[0].quantity, 10);
    EXPECT_EQ(reports[0].side,     Side::Buy);
}

TEST(OrderBookAddOrder, PassiveSellRestsAndReturnsNewReport) {
    OrderBook book(Instrument::Rose);
    Order sell = makeOrder("c1", 100.0, 10, Side::Sell);

    auto reports = book.addOrder(sell, "1");

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].status, Status::New);
    EXPECT_EQ(reports[0].side,   Side::Sell);
}

// ─── full fill ────────────────────────────────────────────────────────────────

TEST(OrderBookAddOrder, AggressiveBuyFullyFillsOnePassiveSell) {
    OrderBook book(Instrument::Rose);
    // Rest a sell first
    book.addOrder(makeOrder("seller", 100.0, 10, Side::Sell), "1");

    // Aggressive buy
    auto reports = book.addOrder(makeOrder("buyer", 100.0, 10, Side::Buy), "2");

    // 2 fill reports (passive + aggressor), no New report
    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::Fill);
    EXPECT_EQ(reports[1].status, Status::Fill);
}

TEST(OrderBookAddOrder, AggressiveSellFullyFillsOnePassiveBuy) {
    OrderBook book(Instrument::Rose);
    book.addOrder(makeOrder("buyer", 100.0, 10, Side::Buy), "1");

    auto reports = book.addOrder(makeOrder("seller", 90.0, 10, Side::Sell), "2");

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::Fill);
    EXPECT_EQ(reports[1].status, Status::Fill);
}

// ─── partial fills ────────────────────────────────────────────────────────────

TEST(OrderBookAddOrder, AggressiveBuyPartiallyFillsLargerPassiveSell) {
    OrderBook book(Instrument::Rose);
    book.addOrder(makeOrder("seller", 100.0, 20, Side::Sell), "1");

    // Buy only 10 → passive gets PFill, passive New for remainder not emitted here
    auto reports = book.addOrder(makeOrder("buyer", 100.0, 10, Side::Buy), "2");

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::PFill); // passive partially consumed
    EXPECT_EQ(reports[1].status, Status::Fill);  // aggressor fully filled
}

TEST(OrderBookAddOrder, AggressiveBuyWithRemainderRestsAfterPartialMatch) {
    OrderBook book(Instrument::Rose);
    book.addOrder(makeOrder("seller", 100.0, 10, Side::Sell), "1");

    // Buy 20, only 10 available → 10 fills, 10 rests
    auto reports = book.addOrder(makeOrder("buyer", 100.0, 20, Side::Buy), "2");

    // 2 fill reports + 1 New for the resting remainder
    ASSERT_EQ(reports.size(), 3u);
    EXPECT_EQ(reports[0].status, Status::Fill);  // passive fully consumed
    EXPECT_EQ(reports[1].status, Status::PFill); // aggressor partially filled
    EXPECT_EQ(reports[2].status, Status::New);   // remainder rests
    EXPECT_EQ(reports[2].quantity, 10);           // remainder qty
}

// ─── price priority ───────────────────────────────────────────────────────────

TEST(OrderBookAddOrder, BuyMatchesBestAskFirst) {
    OrderBook book(Instrument::Rose);
    // Two passive sells: better price at 90
    book.addOrder(makeOrder("s1", 100.0, 10, Side::Sell), "1");
    book.addOrder(makeOrder("s2", 90.0,  10, Side::Sell), "2");

    // Buy should match the cheaper sell (s2 at 90) first
    auto reports = book.addOrder(makeOrder("b1", 100.0, 10, Side::Buy), "3");

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0].status, Status::Fill);
    EXPECT_DOUBLE_EQ(reports[0].price, 90.0); // passive price
    EXPECT_STREQ(reports[0].clientOrderId, "s2");
}

TEST(OrderBookAddOrder, SellMatchesBestBidFirst) {
    OrderBook book(Instrument::Rose);
    // Two passive buys: better price at 110
    book.addOrder(makeOrder("b1", 100.0, 10, Side::Buy), "1");
    book.addOrder(makeOrder("b2", 110.0, 10, Side::Buy), "2");

    // Sell should match the higher bid (b2 at 110) first
    auto reports = book.addOrder(makeOrder("s1", 100.0, 10, Side::Sell), "3");

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_DOUBLE_EQ(reports[0].price, 110.0);
    EXPECT_STREQ(reports[0].clientOrderId, "b2");
}

// ─── FIFO (time priority) ─────────────────────────────────────────────────────

TEST(OrderBookAddOrder, SamePriceFifoOrderingForBuys) {
    OrderBook book(Instrument::Rose);
    // Two passive sells at the same price
    book.addOrder(makeOrder("s1", 100.0, 10, Side::Sell), "1"); // first
    book.addOrder(makeOrder("s2", 100.0, 10, Side::Sell), "2"); // second

    // Buy 10 — should match s1 (arrived first)
    auto reports = book.addOrder(makeOrder("b1", 100.0, 10, Side::Buy), "3");

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_STREQ(reports[0].clientOrderId, "s1");
}

TEST(OrderBookAddOrder, SamePriceFifoOrderingForSells) {
    OrderBook book(Instrument::Rose);
    book.addOrder(makeOrder("b1", 100.0, 10, Side::Buy), "1"); // first
    book.addOrder(makeOrder("b2", 100.0, 10, Side::Buy), "2"); // second

    auto reports = book.addOrder(makeOrder("s1", 100.0, 10, Side::Sell), "3");

    ASSERT_EQ(reports.size(), 2u);
    EXPECT_STREQ(reports[0].clientOrderId, "b1");
}

// ─── multi-fill ───────────────────────────────────────────────────────────────

TEST(OrderBookAddOrder, AggressiveBuyFillsMultiplePassiveSells) {
    OrderBook book(Instrument::Rose);
    book.addOrder(makeOrder("s1", 100.0, 10, Side::Sell), "1");
    book.addOrder(makeOrder("s2", 100.0, 10, Side::Sell), "2");
    book.addOrder(makeOrder("s3", 100.0, 10, Side::Sell), "3");

    auto reports = book.addOrder(makeOrder("b1", 100.0, 30, Side::Buy), "4");

    // 6 fill reports (2 per trade), no New
    ASSERT_EQ(reports.size(), 6u);
    EXPECT_EQ(reports[5].status, Status::Fill); // aggressor fully filled on last trade
}

// ─── no cross ─────────────────────────────────────────────────────────────────

TEST(OrderBookAddOrder, NoCrossDoesNotTriggerMatch) {
    OrderBook book(Instrument::Rose);
    book.addOrder(makeOrder("s1", 110.0, 10, Side::Sell), "1");

    // Buy at 100 < ask 110 → no match, rests
    auto reports = book.addOrder(makeOrder("b1", 100.0, 10, Side::Buy), "2");

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].status, Status::New);
}
