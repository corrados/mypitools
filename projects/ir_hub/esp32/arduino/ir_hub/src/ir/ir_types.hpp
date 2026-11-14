#pragma once

#include <cstdint>

namespace irhub::ir {

enum class Device : uint8_t {
    Bar,
    Beam,
    Led,
    Tv,
    Dvd,
    FireTvBeam,
    Unknown,
};

struct Pulse {
    bool mark;
    uint32_t duration_us;
};

const char* device_name(Device device);

}  // namespace irhub::ir
