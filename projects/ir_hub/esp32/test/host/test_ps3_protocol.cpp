#include <cassert>
#include <cstdint>
#include <cstring>

#include "input/ps3_protocol.hpp"

namespace {

using irhub::input::parse_ps3_bd_report;

void test_eject_report() {
    const uint8_t payload[] = {
        0xA1, 0x00,
        0x00, 0x00, 0x00, 0x16,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x04,
    };
    const auto report = parse_ps3_bd_report(payload, sizeof(payload));
    assert(report.valid);
    assert(!report.release);
    assert(report.has_button);
    assert(report.button_value == 22);
    assert(report.has_battery);
    assert(report.battery == 4);
}

void test_direction_report_without_transaction_header() {
    const uint8_t payload[] = {
        0x00,
        0x10, 0x00, 0x00, 0x54,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x05,
    };
    const auto report = parse_ps3_bd_report(payload, sizeof(payload));
    assert(report.valid);
    assert(report.has_button);
    assert(report.button_value == 268435540);
    assert(report.has_battery);
    assert(report.battery == 5);
}

void test_release_report() {
    const uint8_t payload[] = {
        0xA1, 0x00,
        0x00, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x03,
    };
    const auto report = parse_ps3_bd_report(payload, sizeof(payload));
    assert(report.valid);
    assert(report.release);
    assert(!report.has_button);
    assert(report.has_battery);
    assert(report.battery == 3);
}

void test_invalid_reports() {
    assert(!parse_ps3_bd_report(nullptr, 0).valid);

    const uint8_t short_payload[] = {0xA1, 0x00, 0x00};
    assert(!parse_ps3_bd_report(short_payload, sizeof(short_payload)).valid);

    const uint8_t bad_marker[] = {
        0x00,
        0x00, 0x00, 0x00, 0x01,
        0xFF, 0xFF, 0x00, 0xFF, 0xFF,
        0xFF, 0x05,
    };
    assert(!parse_ps3_bd_report(bad_marker, sizeof(bad_marker)).valid);
}

}  // namespace

int main() {
    test_eject_report();
    test_direction_report_without_transaction_header();
    test_release_report();
    test_invalid_reports();
    return 0;
}
