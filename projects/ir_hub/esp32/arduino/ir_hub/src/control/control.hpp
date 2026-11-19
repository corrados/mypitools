#pragma once

#include <cstdint>

#include "control/state_preview.hpp"
#include "diag/latency.hpp"
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
    uint32_t enqueued_us = 0;
};

struct ControlStats {
    diag::Latency queue_wait;
    diag::Latency handler;
    uint32_t dropped = 0;
};

esp_err_t init();
esp_err_t start();
bool post(const char* button, ButtonSource source);
State current_state();
const ControlStats& stats();
void set_battery_level(uint8_t level);

}  // namespace irhub::control
