#pragma once
#include <cstdint>

enum class Status : uint8_t {
    New = 0,
    Rejected = 1,
    Fill = 2,
    PFill = 3,
};