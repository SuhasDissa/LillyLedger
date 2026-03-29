#include "utils/timestamp.h"
#include <chrono>
#include <cstdio>
#include <ctime>

namespace utils {

std::string currentTransactTime() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    char buf[20];
    std::strftime(buf, 16, "%Y%m%d-%H%M%S", &tm);
    int m = static_cast<int>(ms.count());
    buf[15] = '.';
    buf[16] = static_cast<char>('0' + m / 100);
    buf[17] = static_cast<char>('0' + m / 10 % 10);
    buf[18] = static_cast<char>('0' + m % 10);
    buf[19] = '\0';
    return buf;
}

} 
