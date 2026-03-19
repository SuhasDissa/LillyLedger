#ifndef STATUS_H
#define STATUS_H

#include <cstdint>

enum class Status: uint8_t {
    New,
    Rejected,
    Fill,
    PFill
};

#endif // STATUS_H