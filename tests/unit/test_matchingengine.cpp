#include <gtest/gtest.h>
#include <cstdio>
#include "engine/matchingengine.h"
#include "core/enum/instrument.h"
#include "core/enum/side.h"

// ─── helpers ──────────────────────────────────────────────────────────────────

static Order makeOrder(const char *cid, double price, uint16_t qty,
                       Side side, Instrument instr = Instrument::Rose) {
    Order o{};
    std::snprintf(o.clientOrderId, sizeof(o.clientOrderId), "%s", cid);
    o.price      = price;
    o.quantity   = qty;
    o.side       = side;
    o.instrument = instr;
    return o;
}

static BookEntry makeEntry(const char *cid, const char *oid, double price,
                           uint16_t remaining, Side side,
                           uint32_t seq = 0,
                           Instrument instr = Instrument::Rose) {
    BookEntry e{};
    std::snprintf(e.order.clientOrderId, sizeof(e.order.clientOrderId), "%s", cid);
    e.order.price      = price;
    e.order.quantity   = remaining;
    e.order.side       = side;
    e.order.instrument = instr;
    std::snprintf(e.orderId, sizeof(e.orderId), "%s", oid);
    e.remainingQty = remaining;
    e.seqNum       = seq;
    return e;
}

// ─── isAggressive ─────────────────────────────────────────────────────────────

TEST(MatchingEngineIsAggressive, EmptyOppositeSideReturnsFalse) {
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);
    std::vector<BookEntry> empty;
    EXPECT_FALSE(MatchingEngine::isAggressive(buy, empty));
}

TEST(MatchingEngineIsAggressive, BuyPriceEqualsToBestAskIsAggressive) {
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 10, Side::Sell)};
    EXPECT_TRUE(MatchingEngine::isAggressive(buy, sells));
}

TEST(MatchingEngineIsAggressive, BuyPriceAboveBestAskIsAggressive) {
    Order buy = makeOrder("c1", 110.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 10, Side::Sell)};
    EXPECT_TRUE(MatchingEngine::isAggressive(buy, sells));
}

TEST(MatchingEngineIsAggressive, BuyPriceBelowBestAskIsNotAggressive) {
    Order buy = makeOrder("c1", 90.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 10, Side::Sell)};
    EXPECT_FALSE(MatchingEngine::isAggressive(buy, sells));
}

TEST(MatchingEngineIsAggressive, SellPriceEqualsToBestBidIsAggressive) {
    Order sell = makeOrder("c1", 100.0, 10, Side::Sell);
    std::vector<BookEntry> buys = {makeEntry("b1", "1", 100.0, 10, Side::Buy)};
    EXPECT_TRUE(MatchingEngine::isAggressive(sell, buys));
}

TEST(MatchingEngineIsAggressive, SellPriceBelowBestBidIsAggressive) {
    Order sell = makeOrder("c1", 90.0, 10, Side::Sell);
    std::vector<BookEntry> buys = {makeEntry("b1", "1", 100.0, 10, Side::Buy)};
    EXPECT_TRUE(MatchingEngine::isAggressive(sell, buys));
}

TEST(MatchingEngineIsAggressive, SellPriceAboveBestBidIsNotAggressive) {
    Order sell = makeOrder("c1", 110.0, 10, Side::Sell);
    std::vector<BookEntry> buys = {makeEntry("b1", "1", 100.0, 10, Side::Buy)};
    EXPECT_FALSE(MatchingEngine::isAggressive(sell, buys));
}

// ─── execute ──────────────────────────────────────────────────────────────────

TEST(MatchingEngineExecute, BuyFullyFillsOnePassiveSell) {
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 10, Side::Sell)};
    uint16_t remaining = 10;

    auto trades = MatchingEngine::execute(buy, sells, remaining);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].execQty,   10);
    EXPECT_DOUBLE_EQ(trades[0].execPrice, 100.0);
    EXPECT_EQ(remaining, 0);
    EXPECT_TRUE(sells.empty()); // fully consumed entry removed
}

TEST(MatchingEngineExecute, SellFullyFillsOnePassiveBuy) {
    Order sell = makeOrder("c1", 90.0, 10, Side::Sell);
    std::vector<BookEntry> buys = {makeEntry("b1", "1", 100.0, 10, Side::Buy)};
    uint16_t remaining = 10;

    auto trades = MatchingEngine::execute(sell, buys, remaining);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].execQty,   10);
    EXPECT_DOUBLE_EQ(trades[0].execPrice, 100.0); // passive price
    EXPECT_EQ(remaining, 0);
    EXPECT_TRUE(buys.empty());
}

TEST(MatchingEngineExecute, PartialFillLeavesPassiveRemainder) {
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 20, Side::Sell)};
    uint16_t remaining = 10;

    auto trades = MatchingEngine::execute(buy, sells, remaining);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].execQty, 10);
    EXPECT_EQ(remaining, 0);
    ASSERT_EQ(sells.size(), 1u);
    EXPECT_EQ(sells[0].remainingQty, 10); // 20 - 10
}

TEST(MatchingEngineExecute, AggressorFillsMultiplePassiveOrders) {
    Order buy = makeOrder("c1", 100.0, 30, Side::Buy);
    std::vector<BookEntry> sells = {
        makeEntry("s1", "1", 100.0, 10, Side::Sell, 0),
        makeEntry("s2", "2", 100.0, 10, Side::Sell, 1),
        makeEntry("s3", "3", 100.0, 10, Side::Sell, 2),
    };
    uint16_t remaining = 30;

    auto trades = MatchingEngine::execute(buy, sells, remaining);

    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(remaining, 0);
    EXPECT_TRUE(sells.empty());
}

TEST(MatchingEngineExecute, StopsWhenPriceNoCrosser) {
    Order buy = makeOrder("c1", 100.0, 30, Side::Buy);
    // First entry matches, second does not (ask 110 > bid 100)
    std::vector<BookEntry> sells = {
        makeEntry("s1", "1", 100.0, 10, Side::Sell, 0),
        makeEntry("s2", "2", 110.0, 10, Side::Sell, 1),
    };
    uint16_t remaining = 30;

    auto trades = MatchingEngine::execute(buy, sells, remaining);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(remaining, 20);          // 30 - 10
    ASSERT_EQ(sells.size(), 1u);       // s2 stays
    EXPECT_EQ(std::string(sells[0].orderId), "2");
}

TEST(MatchingEngineExecute, ExecPriceIsPassivePrice) {
    // Buy at 110, passive sell resting at 100 → price improvement → exec at 100
    Order buy = makeOrder("c1", 110.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 10, Side::Sell)};
    uint16_t remaining = 10;

    auto trades = MatchingEngine::execute(buy, sells, remaining);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_DOUBLE_EQ(trades[0].execPrice, 100.0);
}

TEST(MatchingEngineExecute, TradeSnapshotsPassiveRemainingBeforeDecrement) {
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);
    std::vector<BookEntry> sells = {makeEntry("s1", "1", 100.0, 20, Side::Sell)};
    uint16_t remaining = 10;

    auto trades = MatchingEngine::execute(buy, sells, remaining);

    // Snapshot should record the *original* remainingQty (20), not the post-fill one
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].passive.remainingQty, 20);
}

TEST(MatchingEngineExecute, EmptyOppositeSideReturnsNoTrades) {
    Order buy = makeOrder("c1", 100.0, 10, Side::Buy);
    std::vector<BookEntry> empty;
    uint16_t remaining = 10;

    auto trades = MatchingEngine::execute(buy, empty, remaining);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(remaining, 10);
}
