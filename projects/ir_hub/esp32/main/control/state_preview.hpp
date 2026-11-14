#pragma once

#include <cstdint>

#include "util/rgb.hpp"

namespace irhub::control {

enum class State : uint8_t {
    Idle,
    Projector,
    Tv,
    Light,
    Dvd,
    Switch,
};

const char* state_name(State state);
State state_for_button_preview(State current, const char* button);
Rgb state_color(State state, uint8_t value);
Rgb display_color(State state, bool alt_func, uint8_t value);

}  // namespace irhub::control
