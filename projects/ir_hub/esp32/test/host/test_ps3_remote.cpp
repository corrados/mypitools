#include <cassert>
#include <cstdint>
#include <cstring>

#include "input/ps3_remote.hpp"

namespace {

using irhub::input::Ps3RepeatTracker;
using irhub::input::ps3_convert;
using irhub::input::ps3_is_repeat_key;
using irhub::input::ps3_map;

void test_scancodes() {
    const struct {
        uint64_t value;
        const char* name;
    } cases[] = {
        {22, "EJECT"}, {0, "1"}, {1, "2"}, {2, "3"}, {3, "4"}, {4, "5"},
        {5, "6"}, {6, "7"}, {7, "8"}, {8, "9"}, {9, "0"}, {100, "AUIDO"},
        {101, "ANGLE"}, {99, "SUBTITLE"}, {15, "CLEAR"}, {40, "TIME"},
        {129, "RED"}, {130, "GREEN"}, {128, "BLUE"}, {131, "YELLOW"},
        {112, "DISPLAY"}, {26, "TOPMENU"}, {64, "POPUP"}, {14, "RETURN"},
        {1048668, "OPTIONS"}, {2097245, "BACK"}, {268435540, "UP"},
        {1073741910, "DOWN"}, {2147483735, "LEFT"}, {536870997, "RIGHT"},
        {2059, "OK"}, {8388703, "VIEW"}, {4194398, "X"}, {262234, "L1"},
        {65624, "L2"}, {33554513, "L3"}, {524379, "R1"}, {131161, "R2"},
        {67108946, "R3"}, {323, "PLAYSTATION"}, {16777296, "SELECT"},
        {134217811, "START"}, {51, "REWIND"}, {50, "PLAY"}, {52, "FORWARD"},
        {48, "PREVIOUS"}, {56, "STOP"}, {49, "NEXT"}, {96, "SLOWREWIND"},
        {57, "PAUSE"}, {97, "SLOWFORWARD"},
    };
    for (const auto& item : cases) {
        const char* mapped = ps3_map(item.value);
        assert(mapped != nullptr);
        assert(std::strcmp(mapped, item.name) == 0);
    }
    assert(ps3_map(999999999ULL) == nullptr);
}

void test_conversions() {
    assert(std::strcmp(ps3_convert("EJECT"), "POWER") == 0);
    assert(std::strcmp(ps3_convert("L1"), "LIGHT") == 0);
    assert(std::strcmp(ps3_convert("AUIDO"), "MUTE") == 0);
    assert(std::strcmp(ps3_convert("POPUP"), "MENU") == 0);
    assert(std::strcmp(ps3_convert("SLOWREWIND"), "SOURCE") == 0);
    assert(std::strcmp(ps3_convert("SLOWFORWARD"), "SELECT") == 0);
    assert(std::strcmp(ps3_convert("OPTIONS"), "VOL+") == 0);
    assert(std::strcmp(ps3_convert("VIEW"), "VOL-") == 0);
    assert(std::strcmp(ps3_convert("BACK"), "CH+") == 0);
    assert(std::strcmp(ps3_convert("X"), "CH-") == 0);
    assert(std::strcmp(ps3_convert("TOPMENU"), "LIST") == 0);
    assert(std::strcmp(ps3_convert("PLAYSTATION"), "HOME") == 0);
    assert(std::strcmp(ps3_convert("PLAY"), "PLAY") == 0);
    assert(ps3_convert(nullptr) == nullptr);
}

void test_repeat_keys() {
    const char* repeat[] = {"VOL+", "VOL-", "CH+", "CH-", "UP", "DOWN", "LEFT", "RIGHT"};
    for (const char* name : repeat) {
        assert(ps3_is_repeat_key(name));
    }
    assert(!ps3_is_repeat_key("OK"));
    assert(!ps3_is_repeat_key("SELECT"));
    assert(!ps3_is_repeat_key(nullptr));
}

void test_repeat_tracker() {
    Ps3RepeatTracker tracker;
    tracker.press("VOL+", 1000);
    assert(tracker.active());
    assert(!tracker.tick(1699));
    assert(tracker.tick(1700));
    assert(!tracker.tick(1899));
    assert(tracker.tick(1900));
    assert(tracker.tick(2100));
    tracker.release();
    assert(!tracker.active());
    assert(!tracker.tick(9999));

    tracker.press("OK", 100);
    assert(!tracker.active());
    assert(!tracker.tick(10000));
}

}  // namespace

int main() {
    test_scancodes();
    test_conversions();
    test_repeat_keys();
    test_repeat_tracker();
    return 0;
}
