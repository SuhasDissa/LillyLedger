#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include "io/csvwriter.h"
#include "core/executionreport.h"

TEST(CSVWriterTest, WritesSingleReportCorrectly) {
    ExecutionReport report{
        "ord1", "exec1",
        Instrument::Rose, Side::Buy,
        100.0, 10,
        Status::Fill,
        "",
        "20240318-120000.000"
    };

    const std::string outPath = "/tmp/test_csvwriter_output.csv";
    CSVWriter writer(outPath);
    writer.write({report});

    std::ifstream file(outPath);
    ASSERT_TRUE(file.is_open());

    std::string line;
    std::getline(file, line);

    EXPECT_EQ(line, "ord1,exec1,0,1,100,10,2,,20240318-120000.000");
}
