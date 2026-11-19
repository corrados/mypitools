#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace irhub::diag {

struct Latency {
    uint32_t count = 0;
    uint32_t last = 0;
    uint32_t min = 0;
    uint32_t max = 0;
    uint64_t sum = 0;

    void add(uint32_t value) {
        ++count;
        last = value;
        if (count == 1 || value < min) {
            min = value;
        }
        if (value > max) {
            max = value;
        }
        sum += value;
    }
};

struct LatencySnapshot {
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint32_t avg;
};

inline LatencySnapshot snapshot(const Latency& latency) {
    if (latency.count == 0) {
        return {0, 0, 0, 0, 0};
    }
    return {latency.count, latency.last, latency.min, latency.max,
            static_cast<uint32_t>(latency.sum / latency.count)};
}

inline void format(const char* name, const Latency& latency, char* out, size_t size) {
    const LatencySnapshot value = snapshot(latency);
    std::snprintf(out, size, "%s n=%u last=%u avg=%u min=%u max=%u us", name,
                  static_cast<unsigned>(value.count), static_cast<unsigned>(value.last),
                  static_cast<unsigned>(value.avg), static_cast<unsigned>(value.min),
                  static_cast<unsigned>(value.max));
}

}  // namespace irhub::diag
