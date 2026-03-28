#include "io/csvreader.h"
#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

static constexpr int kMaxClientOrderIdChars = 7; 
static constexpr int kMinQuantity = 10;
static constexpr int kMaxQuantity = 1000;
static constexpr int kQuantityStep = 10;
static constexpr int kExpectedFieldCount = 5;

CSVReader::CSVReader(std::string filePath) : filePath_(std::move(filePath)) {}

std::string CSVReader::trim(const std::string &text) {
    const std::string whitespace = " \t\n\r";
    std::size_t start = text.find_first_not_of(whitespace);

    if (start == std::string::npos) {
        return "";
    }

    std::size_t end = text.find_last_not_of(whitespace);

    return text.substr(start, end - start + 1);
}

std::vector<std::string> CSVReader::splitCommaSeparated(const std::string &line) {

    std::vector<std::string> tokens;
    std::size_t start = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ',') {
            std::string token = line.substr(start, i - start);
            tokens.push_back(trim(token));
            start = i + 1;
        }
    }
    std::string lastToken = line.substr(start);
    tokens.push_back(trim(lastToken));
    return tokens;
}

bool CSVReader::isHeaderRow(const std::string &line) {
    std::vector<std::string> tokens = splitCommaSeparated(line);

    if (static_cast<int>(tokens.size()) != kExpectedFieldCount) {
        return false;
    }
    return tokens[0] == "Client Order ID" && tokens[1] == "Instrument" && tokens[2] == "Side" &&
           tokens[3] == "Quantity" && tokens[4] == "Price";
}

ParseResult CSVReader::buildOrder(const RawOrderRow &raw) {
    ParseResult result;
    if (raw.clientOrderId.empty() ||
        raw.clientOrderId.size() > static_cast<std::size_t>(kMaxClientOrderIdChars)) {
        result.reason = "Invalid Client Order Id";
        return result;
    }

    for (char c : raw.clientOrderId) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            result.reason = "Invalid Client Order Id";
            return result;
        }
    }

    Instrument instrument;

    if (raw.instrument == "Rose") {
        instrument = Instrument::Rose;
    } else if (raw.instrument == "Lavender") {
        instrument = Instrument::Lavender;
    } else if (raw.instrument == "Lotus") {
        instrument = Instrument::Lotus;
    } else if (raw.instrument == "Tulip") {
        instrument = Instrument::Tulip;
    } else if (raw.instrument == "Orchid") {
        instrument = Instrument::Orchid;
    } else {
        result.reason = "Invalid Instrument";
        return result;
    }

    int sideValue = 0;

    auto resultParse =
        std::from_chars(raw.side.data(), raw.side.data() + raw.side.size(), sideValue);

    if (resultParse.ec != std::errc() || resultParse.ptr != raw.side.data() + raw.side.size()) {
        result.reason = "Invalid side";
        return result;
    }

    Side side;

    if (sideValue == 1) {
        side = Side::Buy;
    } else if (sideValue == 2) {
        side = Side::Sell;
    } else {
        result.reason = "Invalid side";
        return result;
    }

    int quantityValue = 0;

    auto quantityParse = std::from_chars(raw.quantity.data(),
                                         raw.quantity.data() + raw.quantity.size(), quantityValue);

    if (quantityParse.ec != std::errc() ||
        quantityParse.ptr != raw.quantity.data() + raw.quantity.size()) {
        result.reason = "Invalid quantity";
        return result;
    }

    if (quantityValue < kMinQuantity || quantityValue > kMaxQuantity ||
        quantityValue % kQuantityStep != 0) {
        result.reason = "Invalid quantity";
        return result;
    }

    const char *startPtr = raw.price.c_str();
    char *endPtr = nullptr;

    double priceValue = std::strtod(startPtr, &endPtr);

    if (startPtr == endPtr || *endPtr != '\0') {
        result.reason = "Invalid price";
        return result;
    }

    if (priceValue <= 0.0) {
        result.reason = "Invalid price";
        return result;
    }

    std::snprintf(result.order.clientOrderId, sizeof(result.order.clientOrderId), "%s",
                  raw.clientOrderId.c_str());

    result.order.instrument = instrument;
    result.order.side = side;
    result.order.quantity = static_cast<uint16_t>(quantityValue);
    result.order.price = priceValue;

    result.ok = true;
    return result;
}

ParseResult CSVReader::parseRawRow(const std::string &line) {
    std::vector<std::string> tokens = splitCommaSeparated(line);

    if (static_cast<int>(tokens.size()) != kExpectedFieldCount) {
        ParseResult result;
        result.reason = "Malformed row";
        return result;
    }

    RawOrderRow raw;
    raw.clientOrderId = tokens[0];
    raw.instrument = tokens[1];
    raw.side = tokens[2];
    raw.quantity = tokens[3];
    raw.price = tokens[4];

    ParseResult result = buildOrder(raw);

    if (!result.ok) {
        std::snprintf(result.order.clientOrderId, sizeof(result.order.clientOrderId), "%.7s",
                      raw.clientOrderId.c_str());
    }
    return result;
}

std::vector<ParseResult> CSVReader::readAll() const {
    std::vector<ParseResult> results;
    std::ifstream file(filePath_);

    if (!file.is_open()) {
        ParseResult result;
        result.reason = "Failed to open input file";
        results.push_back(result);
        return results;
    }

    std::string line;
    bool firstNonEmptyLineSeen = false;

    while (std::getline(file, line)) {
        if (trim(line).empty()) {
            continue;
        }
        if (!firstNonEmptyLineSeen) {
            firstNonEmptyLineSeen = true;
            if (isHeaderRow(line)) {
                continue;
            }
        }
        results.push_back(parseRawRow(line));
    }
    return results;
}
