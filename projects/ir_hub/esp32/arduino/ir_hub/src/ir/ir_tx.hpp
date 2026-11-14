#pragma once

#include <cstdint>

#include "esp_err.h"
#include "ir/ir_types.hpp"

namespace irhub::ir {

esp_err_t init();
esp_err_t start();
bool submit(Device device, const char* command, uint8_t repeat = 2);
bool submit_name(const char* full_name, uint8_t repeat = 2);

}  // namespace irhub::ir
