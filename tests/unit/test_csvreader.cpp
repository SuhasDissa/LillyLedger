#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include "io/csvreader.h"
#include "core/enum/instrument.h"
#include "core/enum/side.h"

// Helper to write a temp CSV file
static std::string writeTempCSV(const std::string &content) {
    const std::string path = "/tmp/test_csvreader_input.csv";
    std::ofstream f(path);
    f << content;
    return path;
}

// ─── trim (via parseRawRow with padded fields) ────────────────────────────────

TEST(CSVReaderTest, TrimsWhitespaceAroundFields) {
    // spaces around each field should still parse correctly
    std::string path = writeTempCSV(" ord1 , Rose , 1 , 10 , 100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_STREQ(results[0].order.clientOrderId, "ord1");
}

// ─── header row ──────────────────────────────────────────────────────────────

TEST(CSVReaderTest, SkipsHeaderRow) {
    std::string path = writeTempCSV(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "ord1,Rose,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
}

TEST(CSVReaderTest, SkipsEmptyLines) {
    std::string path = writeTempCSV(
        "\n"
        "ord1,Rose,1,10,100.0\n"
        "\n"
        "ord2,Tulip,2,20,50.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_TRUE(results[1].ok);
}

// ─── valid orders ─────────────────────────────────────────────────────────────

TEST(CSVReaderTest, ParsesAllInstruments) {
    const char *instruments[] = {"Rose", "Lavender", "Lotus", "Tulip", "Orchid"};
    Instrument expected[] = {
        Instrument::Rose, Instrument::Lavender, Instrument::Lotus,
        Instrument::Tulip, Instrument::Orchid
    };

    for (int i = 0; i < 5; ++i) {
        std::string line = std::string("ord1,") + instruments[i] + ",1,10,100.0\n";
        std::string path = writeTempCSV(line);
        CSVReader reader(path);
        auto results = reader.readAll();
        ASSERT_EQ(results.size(), 1u) << "instrument: " << instruments[i];
        EXPECT_TRUE(results[0].ok) << "instrument: " << instruments[i];
        EXPECT_EQ(results[0].order.instrument, expected[i]);
    }
}

TEST(CSVReaderTest, ParsesBuySide) {
    std::string path = writeTempCSV("ord1,Rose,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(results[0].order.side, Side::Buy);
}

TEST(CSVReaderTest, ParsesSellSide) {
    std::string path = writeTempCSV("ord1,Rose,2,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(results[0].order.side, Side::Sell);
}

TEST(CSVReaderTest, ParsesQuantityAndPrice) {
    std::string path = writeTempCSV("ord1,Rose,1,100,99.5\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(results[0].order.quantity, 100);
    EXPECT_DOUBLE_EQ(results[0].order.price, 99.5);
}

// ─── invalid client order id ──────────────────────────────────────────────────

TEST(CSVReaderTest, RejectsEmptyClientOrderId) {
    std::string path = writeTempCSV(",Rose,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid Client Order Id");
}

TEST(CSVReaderTest, RejectsClientOrderIdTooLong) {
    std::string path = writeTempCSV("ord12345,Rose,1,10,100.0\n"); // 8 chars > 7
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid Client Order Id");
}

TEST(CSVReaderTest, RejectsClientOrderIdWithSpecialChars) {
    std::string path = writeTempCSV("ord@1,Rose,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid Client Order Id");
}

TEST(CSVReaderTest, AcceptsMaxLengthClientOrderId) {
    std::string path = writeTempCSV("ord1234,Rose,1,10,100.0\n"); // exactly 7 chars
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
}

// ─── invalid instrument ───────────────────────────────────────────────────────

TEST(CSVReaderTest, RejectsUnknownInstrument) {
    std::string path = writeTempCSV("ord1,Daisy,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid Instrument");
}

// ─── invalid side ─────────────────────────────────────────────────────────────

TEST(CSVReaderTest, RejectsInvalidSideValue) {
    std::string path = writeTempCSV("ord1,Rose,3,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid side");
}

TEST(CSVReaderTest, RejectsNonNumericSide) {
    std::string path = writeTempCSV("ord1,Rose,buy,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid side");
}

// ─── invalid quantity ─────────────────────────────────────────────────────────

TEST(CSVReaderTest, RejectsQuantityBelowMinimum) {
    std::string path = writeTempCSV("ord1,Rose,1,9,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid quantity");
}

TEST(CSVReaderTest, RejectsQuantityAboveMaximum) {
    std::string path = writeTempCSV("ord1,Rose,1,1010,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid quantity");
}

TEST(CSVReaderTest, RejectsQuantityNotMultipleOf10) {
    std::string path = writeTempCSV("ord1,Rose,1,15,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid quantity");
}

TEST(CSVReaderTest, RejectsNonNumericQuantity) {
    std::string path = writeTempCSV("ord1,Rose,1,abc,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid quantity");
}

TEST(CSVReaderTest, AcceptsMinimumQuantity) {
    std::string path = writeTempCSV("ord1,Rose,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(results[0].order.quantity, 10);
}

TEST(CSVReaderTest, AcceptsMaximumQuantity) {
    std::string path = writeTempCSV("ord1,Rose,1,1000,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(results[0].order.quantity, 1000);
}

// ─── invalid price ────────────────────────────────────────────────────────────

TEST(CSVReaderTest, RejectsZeroPrice) {
    std::string path = writeTempCSV("ord1,Rose,1,10,0.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid price");
}

TEST(CSVReaderTest, RejectsNegativePrice) {
    std::string path = writeTempCSV("ord1,Rose,1,10,-5.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid price");
}

TEST(CSVReaderTest, RejectsNonNumericPrice) {
    std::string path = writeTempCSV("ord1,Rose,1,10,abc\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Invalid price");
}

// ─── malformed rows ───────────────────────────────────────────────────────────

TEST(CSVReaderTest, RejectsMalformedRowTooFewColumns) {
    std::string path = writeTempCSV("ord1,Rose,1,10\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Malformed row");
}

TEST(CSVReaderTest, RejectsMalformedRowTooManyColumns) {
    std::string path = writeTempCSV("ord1,Rose,1,10,100.0,extra\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Malformed row");
}

// ─── file I/O ─────────────────────────────────────────────────────────────────

TEST(CSVReaderTest, ReturnsErrorForMissingFile) {
    CSVReader reader("/tmp/nonexistent_file_xyz.csv");
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].reason, "Failed to open input file");
}

TEST(CSVReaderTest, ReturnsEmptyForEmptyFile) {
    std::string path = writeTempCSV("");
    CSVReader reader(path);
    auto results = reader.readAll();
    EXPECT_TRUE(results.empty());
}

TEST(CSVReaderTest, ParsesMultipleRows) {
    std::string path = writeTempCSV(
        "Client Order ID,Instrument,Side,Quantity,Price\n"
        "ord1,Rose,1,10,100.0\n"
        "ord2,Tulip,2,20,50.0\n"
        "bad,,1,10,100.0\n");
    CSVReader reader(path);
    auto results = reader.readAll();
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].ok);
    EXPECT_TRUE(results[1].ok);
    EXPECT_FALSE(results[2].ok);
}
