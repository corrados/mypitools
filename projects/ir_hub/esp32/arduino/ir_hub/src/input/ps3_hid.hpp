#pragma once

#include "esp_err.h"

namespace irhub::input {

esp_err_t init();
bool connected();

}  // namespace irhub::input
