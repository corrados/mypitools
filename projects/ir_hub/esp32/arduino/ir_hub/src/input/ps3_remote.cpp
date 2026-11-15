#include "input/ps3_remote.hpp"

#include <cstring>

namespace irhub::input {

const char* ps3_map(uint64_t value) {
    switch (value) {
        case 22: return "EJECT";
        case 0: return "1";
        case 1: return "2";
        case 2: return "3";
        case 3: return "4";
        case 4: return "5";
        case 5: return "6";
        case 6: return "7";
        case 7: return "8";
        case 8: return "9";
        case 9: return "0";
        case 100: return "AUIDO";
        case 101: return "ANGLE";
        case 99: return "SUBTITLE";
        case 15: return "CLEAR";
        case 40: return "TIME";
        case 129: return "RED";
        case 130: return "GREEN";
        case 128: return "BLUE";
        case 131: return "YELLOW";
        case 112: return "DISPLAY";
        case 26: return "TOPMENU";
        case 64: return "POPUP";
        case 14: return "RETURN";
        case 1048668: return "OPTIONS";
        case 2097245: return "BACK";
        case 268435540: return "UP";
        case 1073741910: return "DOWN";
        case 2147483735: return "LEFT";
        case 536870997: return "RIGHT";
        case 2059: return "OK";
        case 8388703: return "VIEW";
        case 4194398: return "X";
        case 262234: return "L1";
        case 65624: return "L2";
        case 33554513: return "L3";
        case 524379: return "R1";
        case 131161: return "R2";
        case 67108946: return "R3";
        case 323: return "PLAYSTATION";
        case 16777296: return "SELECT";
        case 134217811: return "START";
        case 51: return "REWIND";
        case 50: return "PLAY";
        case 52: return "FORWARD";
        case 48: return "PREVIOUS";
        case 56: return "STOP";
        case 49: return "NEXT";
        case 96: return "SLOWREWIND";
        case 57: return "PAUSE";
        case 97: return "SLOWFORWARD";
        default: return nullptr;
    }
}

const char* ps3_convert(const char* button) {
    if (button == nullptr) {
        return nullptr;
    }
    if (std::strcmp(button, "EJECT") == 0) return "POWER";
    if (std::strcmp(button, "L1") == 0) return "LIGHT";
    if (std::strcmp(button, "AUIDO") == 0) return "MUTE";
    if (std::strcmp(button, "POPUP") == 0) return "MENU";
    if (std::strcmp(button, "SLOWREWIND") == 0) return "SOURCE";
    if (std::strcmp(button, "SLOWFORWARD") == 0) return "SELECT";
    if (std::strcmp(button, "OPTIONS") == 0) return "VOL+";
    if (std::strcmp(button, "VIEW") == 0) return "VOL-";
    if (std::strcmp(button, "BACK") == 0) return "CH+";
    if (std::strcmp(button, "X") == 0) return "CH-";
    if (std::strcmp(button, "TOPMENU") == 0) return "LIST";
    if (std::strcmp(button, "PLAYSTATION") == 0) return "HOME";
    return button;
}

bool ps3_is_repeat_key(const char* button) {
    if (button == nullptr) {
        return false;
    }
    return std::strcmp(button, "VOL+") == 0 ||
           std::strcmp(button, "VOL-") == 0 ||
           std::strcmp(button, "CH+") == 0 ||
           std::strcmp(button, "CH-") == 0 ||
           std::strcmp(button, "UP") == 0 ||
           std::strcmp(button, "DOWN") == 0 ||
           std::strcmp(button, "LEFT") == 0 ||
           std::strcmp(button, "RIGHT") == 0;
}

void Ps3RepeatTracker::press(const char* button, uint32_t now_ms) {
    active_ = ps3_is_repeat_key(button);
    interval_ms_ = 700;
    next_ms_ = now_ms + interval_ms_;
}

void Ps3RepeatTracker::release() {
    active_ = false;
}

bool Ps3RepeatTracker::tick(uint32_t now_ms) {
    if (!active_ || static_cast<int32_t>(now_ms - next_ms_) < 0) {
        return false;
    }
    interval_ms_ = 200;
    next_ms_ = now_ms + interval_ms_;
    return true;
}

}  // namespace irhub::input
