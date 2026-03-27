#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>

#include "io/csvreader.h"
#include "io/csvwriter.h"
#include "engine/orderrouter.h"
#include "engine/executionhandler.h"
#include "core/enum/status.h"

// ─── output row ───────────────────────────────────────────────────────────────

struct OutputRow {
    std::string clientOrderId;
    std::string orderId;
    int         instrument; // 0=Rose 1=Lavender 2=Lotus 3=Tulip 4=Orchid
    int         side;       // 1=Buy 2=Sell
    double      price;
    int         quantity;
    int         status;     // 0=New 1=Rejected 2=Fill 3=PFill
    std::string reason;
    std::string transactTime;
};

// ─── helpers ──────────────────────────────────────────────────────────────────

// Replicates the logic in main.cpp
static std::vector<ExecutionReport> runEngine(const std::string &inputCsv) {
    CSVReader reader(inputCsv);
    auto parseResults = reader.readAll();

    OrderRouter router;
    std::vector<ExecutionReport> allReports;

    for (const auto &result : parseResults) {
        if (!result.ok) {
            allReports.push_back(ExecutionHandler::buildRejectedReport(
                result.order.clientOrderId, result.reason));
        } else {
            auto reports = router.route(result.order);
            allReports.insert(allReports.end(), reports.begin(), reports.end());
        }
    }
    return allReports;
}

// Write input CSV, run full pipeline (including CSVWriter), parse output rows.
static std::vector<OutputRow> runPipeline(const std::string &csvContent,
                                          const std::string &tag) {
    const std::string inPath  = "/tmp/lilly_test_" + tag + "_in.csv";
    const std::string outPath = "/tmp/lilly_test_" + tag + "_out.csv";

    // Write input
    {
        std::ofstream f(inPath);
        f << csvContent;
    }

    // Run engine
    auto reports = runEngine(inPath);

    // Write output via CSVWriter (full pipeline)
    CSVWriter writer(outPath);
    writer.write(reports);

    // Parse output CSV
    std::vector<OutputRow> rows;
    std::ifstream out(outPath);
    std::string line;
    while (std::getline(out, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ','))
            fields.push_back(tok);
        // Pad to 9 fields in case reason is empty (trailing comma)
        while (fields.size() < 9)
            fields.push_back("");

        OutputRow r;
        r.clientOrderId = fields[0];
        r.orderId       = fields[1];
        r.instrument    = std::stoi(fields[2]);
        r.side          = std::stoi(fields[3]);
        r.price         = std::stod(fields[4]);
        r.quantity      = std::stoi(fields[5]);
        r.status        = std::stoi(fields[6]);
        r.reason        = fields[7];
        r.transactTime  = fields[8];
        rows.push_back(r);
    }
    return rows;
}

static bool isValidTimestamp(const std::string &ts) {
    return ts.size() == 19 && ts[8] == '-' && ts[15] == '.';
}

// ─── Scenario 1: passive order rests ─────────────────────────────────────────

TEST(PipelineIntegration, PassiveBuyRestsWithNewReport) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "b1,Rose,1,10,100.0\n",
        "passive_buy");

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].clientOrderId, "b1");
    EXPECT_EQ(rows[0].orderId,       "1");
    EXPECT_EQ(rows[0].instrument,    0); // Rose
    EXPECT_EQ(rows[0].side,          1); // Buy
    EXPECT_DOUBLE_EQ(rows[0].price,  100.0);
    EXPECT_EQ(rows[0].quantity,      10);
    EXPECT_EQ(rows[0].status,        0); // New
    EXPECT_TRUE(rows[0].reason.empty());
    EXPECT_TRUE(isValidTimestamp(rows[0].transactTime));
}

// ─── Scenario 2: full fill ────────────────────────────────────────────────────

TEST(PipelineIntegration, BuyFullyFillsPassiveSell) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,100.0\n"  // sell rests first
        "b1,Rose,1,10,100.0\n", // buy fills it
        "full_fill");

    // sell New, then sell Fill + buy Fill
    ASSERT_EQ(rows.size(), 3u);

    EXPECT_EQ(rows[0].status, 0); // New  (sell rests)
    EXPECT_EQ(rows[0].clientOrderId, "s1");

    EXPECT_EQ(rows[1].status, 2); // Fill (passive sell)
    EXPECT_EQ(rows[1].clientOrderId, "s1");
    EXPECT_EQ(rows[1].side, 2); // Sell
    EXPECT_DOUBLE_EQ(rows[1].price, 100.0);
    EXPECT_EQ(rows[1].quantity, 10);

    EXPECT_EQ(rows[2].status, 2); // Fill (aggressor buy)
    EXPECT_EQ(rows[2].clientOrderId, "b1");
    EXPECT_EQ(rows[2].side, 1); // Buy
    EXPECT_DOUBLE_EQ(rows[2].price, 100.0);
    EXPECT_EQ(rows[2].quantity, 10);
}

// ─── Scenario 3: passive gets partial fill ────────────────────────────────────

TEST(PipelineIntegration, SmallBuyPartiallyFillsLargerPassiveSell) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,20,100.0\n"
        "b1,Rose,1,10,100.0\n",
        "passive_pfill");

    // sell New, then sell PFill + buy Fill
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].status, 0); // New   (sell 20 rests)
    EXPECT_EQ(rows[1].status, 3); // PFill (sell partially consumed)
    EXPECT_EQ(rows[1].quantity, 10);
    EXPECT_EQ(rows[2].status, 2); // Fill  (buy fully consumed)
}

// ─── Scenario 4: aggressor partially filled, remainder rests ──────────────────

TEST(PipelineIntegration, LargeBuyPartiallyFillsThenRests) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,100.0\n"
        "b1,Rose,1,20,100.0\n",
        "aggressor_pfill");

    // sell New, then sell Fill + buy PFill + buy New (remainder 10)
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].status, 0); // New   (sell 10 rests)
    EXPECT_EQ(rows[1].status, 2); // Fill  (sell fully consumed)
    EXPECT_EQ(rows[2].status, 3); // PFill (buy 10/20 filled)
    EXPECT_EQ(rows[2].quantity, 10);
    EXPECT_EQ(rows[3].status, 0); // New   (buy remainder 10 rests)
    EXPECT_EQ(rows[3].quantity, 10);
    EXPECT_EQ(rows[3].clientOrderId, "b1");
}

// ─── Scenario 5: invalid order is rejected ────────────────────────────────────

TEST(PipelineIntegration, InvalidInstrumentProducesRejectedReport) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "bad1,Jasmine,1,10,100.0\n",
        "rejection");

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].status, 1); // Rejected
    EXPECT_EQ(rows[0].clientOrderId, "bad1");
    EXPECT_FALSE(rows[0].reason.empty());
    EXPECT_TRUE(isValidTimestamp(rows[0].transactTime));
}

// ─── Scenario 6: mixed valid and invalid orders ───────────────────────────────

TEST(PipelineIntegration, MixedValidAndInvalidOrdersPreservesOrder) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "bad1,Jasmine,1,10,100.0\n"   // rejected
        "ok1,Tulip,1,10,50.0\n",      // valid, rests
        "mixed");

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].status, 1); // Rejected
    EXPECT_EQ(rows[0].clientOrderId, "bad1");
    EXPECT_EQ(rows[1].status, 0); // New
    EXPECT_EQ(rows[1].clientOrderId, "ok1");
}

TEST(PipelineIntegration, MultipleRejections) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "x1,Jasmine,1,10,100.0\n"   // bad instrument
        "x2,Rose,9,10,100.0\n"      // bad side
        "x3,Rose,1,5,100.0\n"       // qty below min
        "x4,Rose,1,10,-1.0\n",      // negative price
        "multi_reject");

    ASSERT_EQ(rows.size(), 4u);
    for (const auto &r : rows)
        EXPECT_EQ(r.status, 1); // all Rejected
}

// ─── Scenario 7: price improvement (exec at passive price) ───────────────────

TEST(PipelineIntegration, AggressiveBuyGetsPriceImprovement) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,90.0\n"    // sell rests at 90
        "b1,Rose,1,10,110.0\n",  // buy bids 110, executes at 90
        "price_improvement");

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[1].price, 90.0); // passive fill at 90
    EXPECT_DOUBLE_EQ(rows[2].price, 90.0); // aggressor also at 90
}

// ─── Scenario 8: price priority ───────────────────────────────────────────────

TEST(PipelineIntegration, BuyMatchesBestAskBeforeWorseAsk) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,110.0\n"  // worse ask
        "s2,Rose,2,10,100.0\n"  // better ask
        "b1,Rose,1,10,110.0\n", // buy hits best ask (s2 at 100)
        "price_priority");

    // s1 New, s2 New, then s2 Fill + b1 Fill (s1 stays in book)
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].status, 0); // New s1
    EXPECT_EQ(rows[1].status, 0); // New s2
    EXPECT_EQ(rows[2].status, 2); // Fill — passive is s2 (better price)
    EXPECT_EQ(rows[2].clientOrderId, "s2");
    EXPECT_DOUBLE_EQ(rows[2].price, 100.0);
    EXPECT_EQ(rows[3].status, 2); // Fill — aggressor b1
}

// ─── Scenario 9: FIFO time priority at same price ────────────────────────────

TEST(PipelineIntegration, SamePriceFifoTimeOrdering) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,100.0\n"  // first at this price
        "s2,Rose,2,10,100.0\n"  // second at this price
        "b1,Rose,1,10,100.0\n", // should match s1 (arrived first)
        "fifo");

    // s1 New, s2 New, s1 Fill, b1 Fill  (s2 stays in book)
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[2].clientOrderId, "s1"); // FIFO: s1 matched first
    EXPECT_EQ(rows[2].status, 2);           // Fill
    EXPECT_EQ(rows[3].status, 2);           // Fill for buyer
}

// ─── Scenario 10: multi-instrument isolation ─────────────────────────────────

TEST(PipelineIntegration, DifferentInstrumentsDontMatch) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,100.0\n"
        "b1,Tulip,1,10,100.0\n",
        "cross_instrument");

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].status, 0); // New (Rose sell)
    EXPECT_EQ(rows[1].status, 0); // New (Tulip buy) — no match
}

TEST(PipelineIntegration, AllFiveInstrumentsMaintainSeparateBooks) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "r1,Rose,2,10,100.0\n"
        "l1,Lavender,2,10,100.0\n"
        "lo1,Lotus,2,10,100.0\n"
        "t1,Tulip,2,10,100.0\n"
        "o1,Orchid,2,10,100.0\n"
        "rb1,Rose,1,10,100.0\n"      // only Rose matches
        "lb1,Lavender,1,10,100.0\n", // only Lavender matches
        "five_books");

    // 5 New reports, then 2 fills each for Rose and Lavender = 5+4 = 9
    ASSERT_EQ(rows.size(), 9u);

    // First 5 are all New
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(rows[i].status, 0);

    // Rose match: rows 5 and 6
    EXPECT_EQ(rows[5].status, 2);
    EXPECT_EQ(rows[5].instrument, 0); // Rose
    EXPECT_EQ(rows[6].status, 2);
    EXPECT_EQ(rows[6].instrument, 0);

    // Lavender match: rows 7 and 8
    EXPECT_EQ(rows[7].status, 2);
    EXPECT_EQ(rows[7].instrument, 1); // Lavender
    EXPECT_EQ(rows[8].status, 2);
    EXPECT_EQ(rows[8].instrument, 1);
}

// ─── Scenario 11: aggressor fills multiple passive orders ─────────────────────

TEST(PipelineIntegration, OneBuyFillsThreePassiveSells) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "s1,Rose,2,10,100.0\n"
        "s2,Rose,2,10,100.0\n"
        "s3,Rose,2,10,100.0\n"
        "b1,Rose,1,30,100.0\n",
        "multi_fill");

    // 3 New + 6 Fill reports = 9
    ASSERT_EQ(rows.size(), 9u);

    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(rows[i].status, 0); // New

    // Each trade: passive Fill + aggressor PFill or Fill
    EXPECT_EQ(rows[3].status, 2); // s1 Fill
    EXPECT_EQ(rows[4].status, 3); // b1 PFill (10/30)
    EXPECT_EQ(rows[5].status, 2); // s2 Fill
    EXPECT_EQ(rows[6].status, 3); // b1 PFill (20/30)
    EXPECT_EQ(rows[7].status, 2); // s3 Fill
    EXPECT_EQ(rows[8].status, 2); // b1 Fill  (30/30)
}

// ─── Scenario 12: orderId increments across instruments ───────────────────────

TEST(PipelineIntegration, OrderIdIncrementsCrossInstrument) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "c1,Rose,1,10,100.0\n"
        "c2,Tulip,2,10,100.0\n"
        "c3,Orchid,1,10,100.0\n",
        "orderid_counter");

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].orderId, "1");
    EXPECT_EQ(rows[1].orderId, "2");
    EXPECT_EQ(rows[2].orderId, "3");
}

// ─── Scenario 13: header-only file ───────────────────────────────────────────

TEST(PipelineIntegration, HeaderOnlyFileProducesNoReports) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n",
        "header_only");

    EXPECT_TRUE(rows.empty());
}

// ─── Scenario 14: sell-side aggressor ────────────────────────────────────────

TEST(PipelineIntegration, AggressiveSellFillsPassiveBuy) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "b1,Lavender,1,10,100.0\n"  // buy rests
        "s1,Lavender,2,10,90.0\n",  // sell undercuts → aggressive
        "sell_aggressor");

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].status, 0); // New  (buy rests)
    EXPECT_EQ(rows[1].status, 2); // Fill (passive buy)
    EXPECT_EQ(rows[1].side,    1); // Buy side
    EXPECT_DOUBLE_EQ(rows[1].price, 100.0); // executed at passive (buy) price
    EXPECT_EQ(rows[2].status, 2); // Fill (aggressor sell)
    EXPECT_DOUBLE_EQ(rows[2].price, 100.0);
}

// ─── Scenario 15: quantity validation for invalid orders ─────────────────────

TEST(PipelineIntegration, InvalidQuantityRejectedBeforeRouting) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "c1,Rose,1,5,100.0\n"    // qty 5 < min 10
        "c2,Rose,1,1010,100.0\n" // qty > max 1000
        "c3,Rose,1,15,100.0\n",  // qty not multiple of 10
        "qty_validation");

    ASSERT_EQ(rows.size(), 3u);
    for (const auto &r : rows)
        EXPECT_EQ(r.status, 1); // all Rejected
}

// ─── Scenario 16: whitespace in input is handled correctly ───────────────────

TEST(PipelineIntegration, WhitespaceTrimmedFromInputFields) {
    auto rows = runPipeline(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        " c1 , Rose , 1 , 10 , 100.0 \n",
        "whitespace");

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].status, 0); // New — parsed correctly despite whitespace
    EXPECT_EQ(rows[0].clientOrderId, "c1");
}
