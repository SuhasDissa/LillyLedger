#include "io/csvwriter.h"
#include <vector>
#include "core/executionreport.h"
#include <fstream>

CSVWriter::CSVWriter(std::string filePath) : filePath_(std::move(filePath)) {}

void CSVWriter::write(const std::vector<ExecutionReport> &reports) const {
  std::ofstream file(filePath_);

  if (!file.is_open()) {
    return;
  }

  for (ExecutionReport report : reports) {
    file << report.clientOrderId << ","
         << report.orderId << ","
         << static_cast<int>(report.instrument) << ","
         << static_cast<int>(report.side) << ","
         << report.price << ","
         << report.quantity << ","
         << static_cast<int>(report.status) << ","
         << report.reason << ","
         << report.transactTime
         << std::endl;
  }
}