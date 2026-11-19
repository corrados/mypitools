#pragma once

#include <cstddef>

#include "diag/latency.hpp"
#include "esp_err.h"

namespace irhub::web_server {

struct WebStats {
    diag::Latency handle;
    diag::Latency tx;
    uint32_t dropped = 0;
};

esp_err_t start();
size_t format_stats(char* out, size_t size);

}  // namespace irhub::web_server
