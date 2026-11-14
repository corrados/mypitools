#pragma once

#include <cstdint>

#include "control/state_preview.hpp"
#include "esp_err.h"

namespace irhub::control {

enum class ButtonSource : uint8_t {
    Bluetooth,
    ScriptSocket,
    Internal,
};

struct ControlEvent {
    ButtonSource source;
    char button[32];
};

esp_err_t init();
esp_err_t start();
bool post(const char* button, ButtonSource source);

}  // namespace irhub::control
