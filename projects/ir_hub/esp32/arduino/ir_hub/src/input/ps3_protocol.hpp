#pragma once

#include <cstddef>
#include <cstdint>

namespace irhub::input {

struct Ps3Report {
    bool valid = false;
    bool release = false;
    bool has_button = false;
    bool has_battery = false;
    uint64_t button_value = 0;
    uint8_t battery = 0;
};

Ps3Report parse_ps3_bd_report(const uint8_t* data, size_t length);

}  // namespace irhub::input
