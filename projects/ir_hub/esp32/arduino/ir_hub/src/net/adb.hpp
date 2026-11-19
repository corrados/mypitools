#pragma once

#include <string>

#include "diag/latency.hpp"
#include "esp_err.h"

namespace irhub::adb {

struct AdbStats {
    diag::Latency queue_wait;
    diag::Latency round_trip;
    uint32_t dropped = 0;
    uint32_t timeouts = 0;
};

esp_err_t init();
esp_err_t start();
bool submit(const char* command);
const AdbStats& stats();
bool connected();
bool public_key_line(std::string& out);
std::string public_key_fingerprint();

}  // namespace irhub::adb
