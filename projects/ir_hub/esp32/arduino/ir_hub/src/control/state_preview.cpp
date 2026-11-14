#include "control/state_preview.hpp"

#include <cstring>

namespace irhub::control {

const char* state_name(State state) {
    switch (state) {
        case State::Idle:
            return "IDLE";
        case State::Projector:
            return "PROJECTOR";
        case State::Tv:
            return "TV";
        case State::Light:
            return "LIGHT";
        case State::Dvd:
            return "DVD";
        case State::Switch:
            return "SWITCH";
    }
    return "UNKNOWN";
}

State state_for_button_preview(State current, const char* button) {
    if (button == nullptr) {
        return current;
    }
    if (std::strcmp(button, "1") == 0) {
        return State::Projector;
    }
    if (std::strcmp(button, "2") == 0) {
        return State::Tv;
    }
    if (std::strcmp(button, "3") == 0) {
        return State::Light;
    }
    if (std::strcmp(button, "4") == 0) {
        return State::Dvd;
    }
    if (std::strcmp(button, "5") == 0) {
        return State::Switch;
    }
    if (std::strcmp(button, "POWER") == 0) {
        return State::Idle;
    }
    if (std::strcmp(button, "LIGHT") == 0) {
        if (current == State::Idle) {
            return State::Light;
        }
        if (current == State::Light) {
            return State::Idle;
        }
    }
    return current;
}

Rgb state_color(State state, uint8_t value) {
    switch (state) {
        case State::Projector:
            return {0, 0, value};
        case State::Tv:
            return {0, value, 0};
        case State::Light:
            return {value, value, value};
        case State::Dvd:
            return {value, value, 0};
        case State::Switch:
            return {value, 0, value};
        case State::Idle:
            return {0, 0, 0};
    }
    return {0, 0, 0};
}

Rgb display_color(State state, bool alt_func, uint8_t value) {
    if (alt_func && state != State::Idle) {
        return {255, 255, 255};
    }
    return state_color(state, value);
}

}  // namespace irhub::control
