#include <iostream>
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include "engine/orderrouter.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: lillyledger <input.csv> <output.csv>\n";
        return 1;
    }

    CSVReader reader(argv[1]);
    auto parseResults = reader.readAll();

    OrderRouter router;
    std::vector<ExecutionReport> allReports;

    for (const auto &result : parseResults) {
        if (!result.ok) {
            // Emit a rejected execution report for invalid orders.
            ExecutionReport r{};
            std::snprintf(r.clientOrderId, sizeof(r.clientOrderId), "%s",
                          result.order.clientOrderId);
            r.status = Status::Rejected;
            std::snprintf(r.reason, sizeof(r.reason), "%s",
                          result.reason.c_str());
            allReports.push_back(r);
        } else {
            auto reports = router.route(result.order);
            allReports.insert(allReports.end(), reports.begin(), reports.end());
        }
    }

    CSVWriter writer(argv[2]);
    writer.write(allReports);

    return 0;
}
