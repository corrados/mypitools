#pragma once

#include <cstdint>

#include "esp_err.h"

namespace irhub::rgb_led {

esp_err_t init();
void set(uint8_t r, uint8_t g, uint8_t b);

}  // namespace irhub::rgb_led
