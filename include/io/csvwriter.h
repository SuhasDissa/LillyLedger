#pragma once
#include "core/executionreport.h"
#include <string>
#include <vector>

class CSVWriter {
  public:
    explicit CSVWriter(std::string filePath);

    void write(const std::vector<ExecutionReport> &reports) const;

  private:
    std::string filePath_;
};