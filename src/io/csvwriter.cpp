#include "io/csvwriter.h"
#include "core/executionreport.h"
#include <fstream>
#include <iostream>
#include <vector>

CSVWriter::CSVWriter(std::string filePath) : filePath_(std::move(filePath)) {}

void CSVWriter::write(const std::vector<ExecutionReport> &reports) const {
    std::ofstream file(filePath_);

    if (!file.is_open()) {
        std::cerr << "CSVWriter: failed to open output file: " << filePath_ << '\n';
        return;
    }

    for (const auto &r : reports) {
        file << r.clientOrderId << ',' << r.orderId << ',' << static_cast<int>(r.instrument) << ','
             << static_cast<int>(r.side) << ',' << r.price << ',' << r.quantity << ','
             << static_cast<int>(r.status) << ',' << r.reason << ',' << r.transactTime << '\n';
    }
}