#pragma once

#include "esp_err.h"

namespace irhub::input {

esp_err_t init();
esp_err_t start_pairing();
bool connected();

}  // namespace irhub::input
