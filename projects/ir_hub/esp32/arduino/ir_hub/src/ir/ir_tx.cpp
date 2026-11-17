#include "ir/ir_tx.hpp"

#include <Arduino.h>

#include <cctype>
#include <cstring>

#include "config.hpp"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ir/ir_codes.hpp"
#include "ir/ir_waveform.hpp"

namespace irhub::ir {
namespace {

constexpr char TAG[] = "ir";
constexpr size_t kQueueLength = 16;
constexpr size_t kTaskStack = 3072;
constexpr UBaseType_t kTaskPriority = 10;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr size_t kRmtMemBlockSymbols = 64;
constexpr size_t kRmtQueueDepth = 8;
constexpr size_t kMaxSymbols = 192;
constexpr size_t kMaxEntries = 320;
constexpr uint32_t kMaxPulseUs = 32767;

struct Request {
    Device device;
    char command[24];
    uint8_t repeat;
};

struct Entry {
    bool level;
    uint16_t duration;
};

QueueHandle_t s_queue = nullptr;
rmt_channel_handle_t s_channel = nullptr;
rmt_encoder_handle_t s_encoder = nullptr;
Waveform s_waveform;
Entry s_entries[kMaxEntries];
rmt_symbol_word_t s_symbols[kMaxSymbols];
bool s_toggle = false;

Device parse_device(const char* name) {
    if (std::strcmp(name, "BAR") == 0) {
        return Device::Bar;
    }
    if (std::strcmp(name, "BEAM") == 0) {
        return Device::Beam;
    }
    if (std::strcmp(name, "LED") == 0) {
        return Device::Led;
    }
    if (std::strcmp(name, "TV") == 0) {
        return Device::Tv;
    }
    if (std::strcmp(name, "DVD") == 0) {
        return Device::Dvd;
    }
    if (std::strcmp(name, "FIRETVBEAM") == 0) {
        return Device::FireTvBeam;
    }
    return Device::Unknown;
}

size_t to_symbols(const Waveform& waveform) {
    size_t count = 0;
    for (const Pulse& pulse : waveform) {
        uint32_t remaining = pulse.duration_us;
        while (remaining > 0 && count < kMaxEntries) {
            const uint16_t chunk =
                static_cast<uint16_t>(remaining > kMaxPulseUs ? kMaxPulseUs : remaining);
            s_entries[count++] = {pulse.mark, chunk};
            remaining -= chunk;
        }
    }

    if (count == 0) {
        return 0;
    }
    if ((count % 2) != 0) {
        if (count >= kMaxEntries) {
            --count;
        } else {
            s_entries[count] = {s_entries[count - 1].level, 1};
            ++count;
        }
    }

    const size_t symbol_count = count / 2;
    for (size_t i = 0; i < symbol_count; ++i) {
        const Entry& first = s_entries[i * 2];
        const Entry& second = s_entries[i * 2 + 1];
        s_symbols[i].level0 = first.level ? 1 : 0;
        s_symbols[i].duration0 = first.duration;
        s_symbols[i].level1 = second.level ? 1 : 0;
        s_symbols[i].duration1 = second.duration;
    }
    return symbol_count;
}

bool apply_carrier(uint32_t frequency_hz, float duty_cycle = 0.5f) {
    rmt_carrier_config_t carrier = {};
    carrier.frequency_hz = frequency_hz;
    carrier.duty_cycle = duty_cycle;
    carrier.flags.polarity_active_low = false;
    carrier.flags.always_on = false;

    const esp_err_t err = rmt_apply_carrier(s_channel, &carrier);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_apply_carrier(%lu) failed: %s", static_cast<unsigned long>(frequency_hz),
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

void ir_task(void*) {
    Request request = {};
    while (true) {
        if (xQueueReceive(s_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (request.device == Device::Bar) {
            s_toggle = !s_toggle;
        }

        const char* code = nullptr;
        if (!find_code(request.device, request.command, code)) {
            ESP_LOGW(TAG, "unknown code %s %s", device_name(request.device), request.command);
            continue;
        }

        const bool bar = request.device == Device::Bar;
        const bool built = bar ? build_bar_long_gap_waveform(code, s_toggle, s_waveform)
                               : build_waveform(request.device, code, s_toggle, s_waveform);
        if (!built) {
            ESP_LOGW(TAG, "cannot build waveform for %s %s", device_name(request.device),
                     request.command);
            continue;
        }

        const uint32_t frequency_hz = carrier_frequency_hz(request.device);
        const float duty_cycle = bar ? 0.33f : 0.5f;
        if (!apply_carrier(frequency_hz, duty_cycle)) {
            continue;
        }

        const size_t symbol_count = to_symbols(s_waveform);
        if (symbol_count == 0) {
            continue;
        }

        ESP_LOGI(TAG, "send %s %s repeat=%u", device_name(request.device), request.command,
                 static_cast<unsigned>(request.repeat));
        for (uint8_t i = 0; i < request.repeat; ++i) {
            rmt_transmit_config_t transmit = {};
            transmit.loop_count = 0;
            const esp_err_t err =
                rmt_transmit(s_channel, s_encoder, s_symbols,
                             symbol_count * sizeof(rmt_symbol_word_t), &transmit);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
                break;
            }
            if (rmt_tx_wait_all_done(s_channel, 1000) != ESP_OK) {
                ESP_LOGE(TAG, "rmt_tx_wait_all_done timed out");
                break;
            }
        }
    }
}

}  // namespace

esp_err_t init() {
    s_queue = xQueueCreate(kQueueLength, sizeof(Request));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    rmt_tx_channel_config_t channel = {};
    channel.gpio_num = static_cast<gpio_num_t>(config::kIrPin);
    channel.clk_src = RMT_CLK_SRC_DEFAULT;
    channel.resolution_hz = kRmtResolutionHz;
    channel.mem_block_symbols = kRmtMemBlockSymbols;
    channel.trans_queue_depth = kRmtQueueDepth;
    channel.intr_priority = 0;
    channel.flags.invert_out = false;
    channel.flags.with_dma = false;

    esp_err_t err = rmt_new_tx_channel(&channel, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    rmt_copy_encoder_config_t encoder = {};
    err = rmt_new_copy_encoder(&encoder, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!apply_carrier(38000)) {
        return ESP_FAIL;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    s_waveform.reserve(128);
    s_toggle = false;
    return ESP_OK;
}

esp_err_t start() {
    if (s_queue == nullptr || s_channel == nullptr || s_encoder == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result =
        xTaskCreate(ir_task, "ir", kTaskStack, nullptr, kTaskPriority, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool submit(Device device, const char* command, uint8_t repeat) {
    if (s_queue == nullptr || command == nullptr || command[0] == '\0') {
        return false;
    }

    Request request = {};
    request.device = device;
    request.repeat = repeat;
    std::strncpy(request.command, command, sizeof(request.command) - 1);
    request.command[sizeof(request.command) - 1] = '\0';

    return xQueueSend(s_queue, &request, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool submit_name(const char* full_name, uint8_t repeat) {
    if (full_name == nullptr) {
        return false;
    }

    char normalized[64] = {};
    size_t length = 0;
    for (const char* c = full_name; *c != '\0' && length < sizeof(normalized) - 1; ++c) {
        normalized[length++] =
            static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    }
    normalized[length] = '\0';

    char* start = normalized;
    while (*start == ' ') {
        ++start;
    }
    char* end = start + std::strlen(start);
    while (end > start && end[-1] == ' ') {
        --end;
    }
    *end = '\0';

    if (std::strstr(start, "UNKNOWN") != nullptr) {
        return false;
    }

    char* separator = std::strchr(start, ' ');
    if (separator == nullptr) {
        ESP_LOGW(TAG, "missing command in '%s'", start);
        return false;
    }
    *separator = '\0';
    char* command = separator + 1;
    while (*command == ' ') {
        ++command;
    }
    if (*command == '\0') {
        ESP_LOGW(TAG, "missing command in '%s'", start);
        return false;
    }

    const Device device = parse_device(start);
    if (device == Device::Unknown) {
        ESP_LOGW(TAG, "unknown device '%s'", start);
        return false;
    }
    if (device == Device::FireTvBeam) {
        ESP_LOGW(TAG, "FIRETVBEAM is handled by ADB, not IR");
        return false;
    }

    return submit(device, command, repeat);
}

}  // namespace irhub::ir
