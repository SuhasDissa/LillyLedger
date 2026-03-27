#include "engine/executionhandler.h"
#include "engine/orderrouter.h"
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include <chrono>
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: lillyledger <input.csv> <output.csv>\n";
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();

    CSVReader reader(argv[1]);
    auto parseResults = reader.readAll();

    auto t1 = std::chrono::steady_clock::now();

    OrderRouter router;
    std::vector<ExecutionReport> allReports;

    for (const auto &result : parseResults) {
        if (!result.ok) {
            allReports.push_back(
                ExecutionHandler::buildRejectedReport(result.order.clientOrderId, result.reason));
        } else {
            auto reports = router.route(result.order);
            allReports.insert(allReports.end(), reports.begin(), reports.end());
        }
    }

    auto t2 = std::chrono::steady_clock::now();

    CSVWriter writer(argv[2]);
    writer.write(allReports);

    auto t3 = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    };
    std::cerr << "[perf] parse=" << ms(t0, t1) << "us"
              << " match=" << ms(t1, t2) << "us"
              << " write=" << ms(t2, t3) << "us"
              << " total=" << ms(t0, t3) << "us"
              << " orders=" << parseResults.size() << " reports=" << allReports.size() << '\n';

    return 0;
}
