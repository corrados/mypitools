#pragma once

#include "esp_err.h"

namespace irhub::adb {

esp_err_t init();
esp_err_t start();
bool submit(const char* command);
bool connected();

}  // namespace irhub::adb
