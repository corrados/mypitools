#include "control/control.hpp"

#include <Arduino.h>

#include <atomic>
#include <cstring>

#include "config.hpp"
#include "control/state_machine.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ir/ir_tx.hpp"
#include "net/adb.hpp"
#include "net/radio_socket.hpp"
#include "output/rgb_led.hpp"

namespace irhub::control {
namespace {

constexpr char TAG[] = "control";
constexpr size_t kQueueLength = 16;
constexpr size_t kSequenceQueueLength = 16;
constexpr size_t kTaskStack = 3072;
constexpr size_t kSequenceTaskStack = 2048;
constexpr UBaseType_t kTaskPriority = 8;
constexpr UBaseType_t kSequencePriority = 6;
constexpr size_t kSequenceWorkers = 2;

struct SequenceJob {
    SequenceId id;
    State state;
    const char* argument;
};

QueueHandle_t s_queue = nullptr;
QueueHandle_t s_sequence_queue = nullptr;
MachineState s_machine = initial_machine_state();
std::atomic<State> s_shared_state{State::Idle};
std::atomic<bool> s_radio_timer_active{false};
esp_timer_handle_t s_radio_timer = nullptr;
std::atomic<uint8_t> s_battery_level{5};

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

void send_ir(ir::Device device, const char* command, uint8_t repeat) {
    if (device == ir::Device::FireTvBeam) {
        if (!adb::submit(command)) {
            ESP_LOGW(TAG, "ADB unavailable: FIRETVBEAM %s", command);
        }
        return;
    }
    if (!ir::submit(device, command, repeat)) {
        ESP_LOGW(TAG, "IR queue full: %s %s", ir::device_name(device), command);
    }
}

void radio_off_timer_cb(void*) {
    s_radio_timer_active.store(false);
    if (!radio_socket::switch_power(false)) {
        ESP_LOGW(TAG, "radio-off queue full");
    }
}

void reset_radio_off_timer() {
    s_radio_timer_active.store(false);
    if (s_radio_timer != nullptr) {
        esp_timer_stop(s_radio_timer);
        const esp_err_t err =
            esp_timer_start_once(s_radio_timer, static_cast<uint64_t>(config::kStandbyMs) * 1000ULL);
        if (err == ESP_OK) {
            s_radio_timer_active.store(true);
        } else {
            ESP_LOGW(TAG, "radio timer start failed: %s", esp_err_to_name(err));
        }
    }
}

void cold_start_check() {
    if (s_radio_timer_active.exchange(false)) {
        if (s_radio_timer != nullptr) {
            esp_timer_stop(s_radio_timer);
        }
        ESP_LOGI(TAG, "warm start; radio timer cancelled");
        return;
    }

    ESP_LOGI(TAG, "cold start; battery level %u",
             static_cast<unsigned>(s_battery_level.load()));
    if (s_battery_level.load() < 3) {
        for (int i = 0; i < 6; ++i) {
            rgb_led::set(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            rgb_led::set(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        if (s_battery_level.load() < 4) {
            rgb_led::set(255, 0, 255);
        } else {
            rgb_led::set(0, 0, 255);
        }
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}

bool enqueue_sequence(const Action& action) {
    SequenceJob job = {};
    job.id = action.sequence;
    job.state = action.sequence_state;
    job.argument = action.argument;
    if (xQueueSend(s_sequence_queue, &job, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "sequence queue full");
        return false;
    }
    return true;
}

void run_sequence(const SequenceJob& job) {
    ESP_LOGI(TAG, "sequence=%d state=%s arg=%s", static_cast<int>(job.id),
             state_name(job.state), job.argument == nullptr ? "-" : job.argument);
    switch (job.id) {
        case SequenceId::TvOn:
            send_ir(ir::Device::Tv, "POWERON", 2);
            vTaskDelay(pdMS_TO_TICKS(5000));
            if (s_shared_state.load() == job.state) {
                send_ir(ir::Device::Tv, job.argument, 1);
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
            if (s_shared_state.load() == State::Tv || s_shared_state.load() == State::Switch) {
                send_ir(ir::Device::Tv, "POWERON", 2);
            }
            break;

        case SequenceId::ProjectorOnWithInput:
            send_ir(ir::Device::Beam, "POWER", 2);
            break;

        case SequenceId::ProjectorOff:
            for (int i = 0; i < 7; ++i) {
                send_ir(ir::Device::Beam, "POWER", 1);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;

        case SequenceId::BarOn:
            send_ir(ir::Device::Bar, job.argument, 2);
            vTaskDelay(pdMS_TO_TICKS(15000));
            send_ir(ir::Device::Bar, "SURROUNDON", 2);
            break;

        case SequenceId::LedMaxBrightness:
            for (int i = 0; i < 10; ++i) {
                send_ir(ir::Device::Led, "BRIGHTER", 1);
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            for (int i = 0; i < 3; ++i) {
                send_ir(ir::Device::Led, "DIMMER", 1);
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            break;
    }
}

void sequence_task(void*) {
    SequenceJob job = {};
    while (true) {
        if (xQueueReceive(s_sequence_queue, &job, portMAX_DELAY) == pdTRUE) {
            run_sequence(job);
        }
    }
}

void execute_action(const Action& action) {
    switch (action.kind) {
        case ActionKind::Ir:
            send_ir(action.device, action.command, action.repeat);
            break;
        case ActionKind::DelayMs:
            vTaskDelay(pdMS_TO_TICKS(action.delay_ms));
            break;
        case ActionKind::RadioPowerOn:
            if (!radio_socket::switch_power(true)) {
                ESP_LOGW(TAG, "radio-on queue full");
            }
            break;
        case ActionKind::ColdStartCheck:
            cold_start_check();
            break;
        case ActionKind::ResetRadioOffTimer:
            reset_radio_off_timer();
            break;
        case ActionKind::StartSequence:
            enqueue_sequence(action);
            break;
    }
}

void handle_event(const ControlEvent& event) {
    const Outcome outcome = process(s_machine, event.button);

    ESP_LOGI(TAG, "button=%s source=%s state=%s alt=%d led=%d transition=%d help=%d actions=%u",
             event.button, source_name(event.source), state_name(s_machine.state),
             s_machine.alt_func ? 1 : 0, s_machine.led_is_on ? 1 : 0,
             outcome.transitioned ? 1 : 0, outcome.help_action ? 1 : 0,
             static_cast<unsigned>(outcome.action_count));

    for (size_t i = 0; i < outcome.action_count; ++i) {
        execute_action(outcome.actions[i]);
    }

    s_machine = outcome.next;
    s_shared_state.store(s_machine.state);

    const Rgb color = display_color(s_machine.state, s_machine.alt_func, config::kRgbValue);
    rgb_led::set(color.r, color.g, color.b);

    ESP_LOGI(TAG, "state=%s prev=%s mapping=%d alt=%d led=%d", state_name(s_machine.state),
             state_name(s_machine.prev_state), static_cast<int>(s_machine.mapping),
             s_machine.alt_func ? 1 : 0, s_machine.led_is_on ? 1 : 0);
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

    s_sequence_queue = xQueueCreate(kSequenceQueueLength, sizeof(SequenceJob));
    if (s_sequence_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_timer_create_args_t timer = {};
    timer.callback = radio_off_timer_cb;
    timer.name = "radio_off";
    const esp_err_t err = esp_timer_create(&timer, &s_radio_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "radio timer create failed: %s", esp_err_to_name(err));
        return err;
    }

    s_machine = initial_machine_state();
    s_shared_state.store(s_machine.state);
    s_radio_timer_active.store(false);
    s_battery_level = 5;

    const Rgb color = display_color(s_machine.state, s_machine.alt_func, config::kRgbValue);
    rgb_led::set(color.r, color.g, color.b);
    return ESP_OK;
}

esp_err_t start() {
    if (s_queue == nullptr || s_sequence_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < kSequenceWorkers; ++i) {
        const BaseType_t result = xTaskCreate(sequence_task, "sequence", kSequenceTaskStack,
                                              nullptr, kSequencePriority, nullptr);
        if (result != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
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

State current_state() {
    return s_shared_state.load();
}

void set_battery_level(uint8_t level) {
    s_battery_level = level > 5 ? 5 : level;
}

}  // namespace irhub::control
