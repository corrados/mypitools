#pragma once

#include "esp_err.h"

namespace irhub::radio_socket {

esp_err_t init();
esp_err_t start();
bool switch_power(bool on);

}  // namespace irhub::radio_socket
