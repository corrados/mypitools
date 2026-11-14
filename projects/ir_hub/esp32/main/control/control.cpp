#include "control/control.hpp"

#include <cstring>

#include "config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "output/rgb_led.hpp"

namespace irhub::control {
namespace {

constexpr char TAG[] = "control";
constexpr size_t kQueueLength = 16;
constexpr size_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 8;

QueueHandle_t s_queue = nullptr;
State s_state = State::Idle;
bool s_alt_func = false;

const char* source_name(ButtonSource source) {
    switch (source) {
        case ButtonSource::Bluetooth:
            return "bluetooth";
        case ButtonSource::ScriptSocket:
            return "script";
        case ButtonSource::Internal:
            return "internal";
    }
    return "unknown";
}

void handle_event(const ControlEvent& event) {
    if (std::strcmp(event.button, "SELECT") == 0 && s_state != State::Idle) {
        s_alt_func = !s_alt_func;
    }

    if (std::strcmp(event.button, "POWER") == 0) {
        s_alt_func = true;
    } else if (std::strcmp(event.button, "1") == 0 || std::strcmp(event.button, "2") == 0 ||
               std::strcmp(event.button, "3") == 0 || std::strcmp(event.button, "4") == 0 ||
               std::strcmp(event.button, "5") == 0) {
        s_alt_func = false;
    }

    const State next = state_for_button_preview(s_state, event.button);
    ESP_LOGI(TAG, "button=%s source=%s state=%s alt=%d", event.button, source_name(event.source),
             state_name(next), s_alt_func ? 1 : 0);
    s_state = next;

    const Rgb color = display_color(s_state, s_alt_func, config::kRgbValue);
    rgb_led_set(color.r, color.g, color.b);
}

void control_task(void*) {
    ControlEvent event = {};
    while (true) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdTRUE) {
            handle_event(event);
        }
    }
}

}  // namespace

esp_err_t init() {
    s_queue = xQueueCreate(kQueueLength, sizeof(ControlEvent));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    s_state = State::Idle;
    s_alt_func = false;
    const Rgb color = display_color(s_state, s_alt_func, config::kRgbValue);
    rgb_led_set(color.r, color.g, color.b);
    return ESP_OK;
}

esp_err_t start() {
    if (s_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result =
        xTaskCreate(control_task, "control", kTaskStack, nullptr, kTaskPriority, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool post(const char* button, ButtonSource source) {
    if (s_queue == nullptr || button == nullptr) {
        return false;
    }

    ControlEvent event = {};
    event.source = source;
    std::strncpy(event.button, button, sizeof(event.button) - 1);
    event.button[sizeof(event.button) - 1] = '\0';

    return xQueueSend(s_queue, &event, pdMS_TO_TICKS(10)) == pdTRUE;
}

}  // namespace irhub::control
