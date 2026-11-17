#include "ir/ir_waveform.hpp"

#include <cstring>

namespace irhub::ir {
namespace {

struct Timing {
    uint32_t frequency_hz;
    uint32_t leading_mark_us;
    uint32_t leading_space_us;
    uint32_t zero_mark_us;
    uint32_t zero_space_us;
    uint32_t one_mark_us;
    uint32_t one_space_us;
    bool trailing_pulse;
    uint32_t trailing_space_us;
};

constexpr uint32_t kRc6UnitUs = 444;

bool timing_for(Device device, Timing& timing) {
    switch (device) {
        case Device::Bar:
            timing = {36000, 0, 0, 0, 0, 0, 0, false, 0};
            return true;
        case Device::Beam:
            timing = {38000, 9044, 4428, 619, 507, 619, 1629, true, 10000};
            return true;
        case Device::Led:
            timing = {38000, 9067, 4439, 604, 519, 604, 1645, true, 110000};
            return true;
        case Device::Tv:
            timing = {38000, 9000, 4500, 619, 507, 619, 1621, true, 0};
            return true;
        case Device::Dvd:
            timing = {38000, 2448, 552, 640, 557, 1240, 557, false, 14400};
            return true;
        case Device::FireTvBeam:
        case Device::Unknown:
            return false;
    }
    return false;
}

void append_mark(Waveform& out, uint32_t duration_us) {
    if (duration_us > 0) {
        out.push_back({true, duration_us});
    }
}

void append_space(Waveform& out, uint32_t duration_us) {
    if (duration_us > 0) {
        out.push_back({false, duration_us});
    }
}

bool build_rc6(const char* code, bool toggle, Waveform& out) {
    const uint32_t t = kRc6UnitUs;

    append_mark(out, 6 * t);
    append_space(out, 2 * t);
    append_mark(out, t);
    append_space(out, t);
    for (int i = 0; i < 3; ++i) {
        append_space(out, t);
        append_mark(out, t);
    }

    if (toggle == 0) {
        append_space(out, 2 * t);
        append_mark(out, 2 * t);
    } else {
        append_mark(out, 2 * t);
        append_space(out, 2 * t);
    }

    for (const char* bit = code; *bit != '\0'; ++bit) {
        if (*bit == '0') {
            append_mark(out, t);
            append_space(out, t);
        } else if (*bit == '1') {
            append_space(out, t);
            append_mark(out, t);
        }
    }

    append_space(out, 6 * t);
    return true;
}

void append_standard(const Timing& timing, const char* code, Waveform& out) {
    append_mark(out, timing.leading_mark_us);
    append_space(out, timing.leading_space_us);

    for (const char* bit = code; *bit != '\0'; ++bit) {
        if (*bit == '0') {
            append_mark(out, timing.zero_mark_us);
            append_space(out, timing.zero_space_us);
        } else if (*bit == '1') {
            append_mark(out, timing.one_mark_us);
            append_space(out, timing.one_space_us);
        }
    }

    if (timing.trailing_pulse) {
        append_mark(out, timing.one_mark_us);
    }
    append_space(out, timing.trailing_space_us);
}

}  // namespace

bool build_waveform(Device device, const char* code, bool toggle, Waveform& out) {
    out.clear();
    if (code == nullptr) {
        return false;
    }

    Timing timing = {};
    if (!timing_for(device, timing)) {
        return false;
    }

    out.reserve(128);
    if (device == Device::Bar) {
        return build_rc6(code, toggle, out);
    }

    append_standard(timing, code, out);
    return true;
}

bool build_bar_long_gap_waveform(const char* code, bool toggle, Waveform& out) {
    out.clear();
    if (!build_waveform(Device::Bar, code, toggle, out)) {
        return false;
    }
    if (!out.empty() && !out.back().mark) {
        out.back().duration_us = 187 * kRc6UnitUs;
    }
    return true;
}

uint32_t carrier_frequency_hz(Device device) {
    Timing timing = {};
    return timing_for(device, timing) ? timing.frequency_hz : 0;
}

}  // namespace irhub::ir
