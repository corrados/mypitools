#include "net/wifi.hpp"

#include <WiFi.h>

#include "config.hpp"
#include "esp_log.h"

namespace irhub::wifi {
namespace {

constexpr char TAG[] = "wifi";

}  // namespace

esp_err_t start() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("ir-hub");
    WiFi.setAutoReconnect(true);

    if (config::kWifiSsid[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID not configured; network features are unavailable");
        return ESP_OK;
    }

    WiFi.begin(config::kWifiSsid, config::kWifiPassword);
    ESP_LOGI(TAG, "connecting to %s", config::kWifiSsid);
    return ESP_OK;
}

}  // namespace irhub::wifi
