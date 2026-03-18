#ifndef CSVWRITER_H
#define CSVWRITER_H

#include <vector>
#include <string>
#include "core/executionreport.h"

class CSVWriter {
public:
  explicit CSVWriter(std::string filePath);
  void write(const std::vector<ExecutionReport> &reports) const;
private:
  std::string filePath_;
};

#endif // CSVWRITER_H