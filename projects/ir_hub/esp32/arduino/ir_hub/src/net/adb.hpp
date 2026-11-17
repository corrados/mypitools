#pragma once

#include <string>

#include "esp_err.h"

namespace irhub::adb {

esp_err_t init();
esp_err_t start();
bool submit(const char* command);
bool connected();
bool public_key_line(std::string& out);
std::string public_key_fingerprint();

}  // namespace irhub::adb
