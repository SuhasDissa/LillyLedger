#include "utils/timestamp.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace utils {

std::string currentTransactTime() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S") << '.'
        << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

} // namespace utils
