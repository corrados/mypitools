#include "control/control.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "input/script_server.hpp"
#include "net/wifi.hpp"
#include "nvs_flash.h"
#include "output/rgb_led.hpp"

namespace {

constexpr char TAG[] = "app";

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

}  // namespace

extern "C" void app_main() {
    ESP_LOGI(TAG, "starting IR hub");
    init_nvs();

    ESP_ERROR_CHECK(rgb_led::init());
    ESP_ERROR_CHECK(control::init());
    ESP_ERROR_CHECK(wifi::start());
    ESP_ERROR_CHECK(control::start());
    ESP_ERROR_CHECK(script_server::start());

    ESP_LOGI(TAG, "IR hub started");
}
