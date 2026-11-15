#include "input/ps3_protocol.hpp"

namespace irhub::input {
namespace {

constexpr size_t kReportLength = 12;

}  // namespace

Ps3Report parse_ps3_bd_report(const uint8_t* data, size_t length) {
    Ps3Report report = {};
    if (data == nullptr) {
        return report;
    }

    if (length >= kReportLength + 1 && data[0] == 0xA1) {
        ++data;
        --length;
    }
    if (length < kReportLength) {
        return report;
    }

    for (size_t i = 5; i <= 9; ++i) {
        if (data[i] != 0xFF) {
            return report;
        }
    }

    report.valid = true;
    const uint8_t battery = data[11] & 0x0F;
    if (battery > 0 && battery <= 5) {
        report.has_battery = true;
        report.battery = battery;
    }

    if (data[4] == 0xFF && data[10] == 0x00) {
        report.release = true;
        return report;
    }

    report.has_button = true;
    report.button_value = (static_cast<uint64_t>(data[1]) << 24) |
                          (static_cast<uint64_t>(data[2]) << 16) |
                          (static_cast<uint64_t>(data[3]) << 8) |
                          static_cast<uint64_t>(data[4]);
    return report;
}

}  // namespace irhub::input
