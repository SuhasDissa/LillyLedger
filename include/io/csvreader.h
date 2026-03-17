#pragma once
#include <string>
#include <vector>

#include "core/order.h"

struct RawOrderRow {
  std::string clientOrderId;
  std::string instrument;
  std::string side;
  std::string quantity;
  std::string price;
};

struct ParseResult {
  bool ok{false};
  Order order{};
  std::string reason;
};

class CSVReader {
public:
  explicit CSVReader(std::string filePath);
  std::vector<ParseResult> readAll() const;

private:
  static std::string trim(const std::string &text);
  static std::vector<std::string> splitCommaSeparated(const std::string &line);
  static bool isHeaderRow(const std::string &line);
  static ParseResult parseRawRow(const std::string &line);
  static ParseResult buildOrder(const RawOrderRow &raw);

  std::string filePath_;
};
