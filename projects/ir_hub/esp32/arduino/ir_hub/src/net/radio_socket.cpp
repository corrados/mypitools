#include "net/radio_socket.hpp"

#include <Arduino.h>

#include <cstdio>

#include "config.hpp"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace irhub::radio_socket {
namespace {

constexpr char TAG[] = "radio";
constexpr size_t kQueueLength = 8;
constexpr size_t kTaskStack = 3072;
constexpr UBaseType_t kTaskPriority = 3;
constexpr int kTimeoutMs = 2000;

QueueHandle_t s_queue = nullptr;

void radio_task(void*) {
    bool on = false;
    while (true) {
        if (xQueueReceive(s_queue, &on, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char url[128] = {};
        std::snprintf(url, sizeof(url), "http://%s/cm?cmnd=Power%%20%s",
                      config::kRadioSocketHost, on ? "On" : "Off");

        esp_http_client_config_t config = {};
        config.url = url;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = kTimeoutMs;

        ESP_LOGI(TAG, "request %s", url);
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == nullptr) {
            ESP_LOGW(TAG, "client init failed");
            continue;
        }

        const esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Power %s -> HTTP %d", on ? "On" : "Off",
                     esp_http_client_get_status_code(client));
        } else {
            ESP_LOGW(TAG, "Power %s failed: %s", on ? "On" : "Off", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }
}

}  // namespace

esp_err_t init() {
    if (s_queue != nullptr) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(kQueueLength, sizeof(bool));
    return s_queue == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t start() {
    if (s_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const BaseType_t result =
        xTaskCreate(radio_task, "radio", kTaskStack, nullptr, kTaskPriority, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool switch_power(bool on) {
    if (s_queue == nullptr) {
        return false;
    }
    return xQueueSend(s_queue, &on, pdMS_TO_TICKS(10)) == pdTRUE;
}

}  // namespace irhub::radio_socket
