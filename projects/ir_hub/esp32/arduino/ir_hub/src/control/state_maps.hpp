#pragma once

#include <cstdint>

#include "control/state_preview.hpp"

namespace irhub::control {

enum class MapId : uint8_t {
    None,
    Projector,
    Tv,
    Light,
    Dvd,
};

bool lookup_state_button(const char* button, State& state);
const char* lookup_command(MapId map, const char* button);
const char* lookup_select_command(const char* button);

}  // namespace irhub::control
