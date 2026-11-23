#include <Arduino.h>
#include <WiFi.h>

#include <cstring>
#include <string>

#include "config.hpp"
#include "control/control.hpp"
#include "esp_log.h"
#include "input/ps3_hid.hpp"
#include "input/ps3_remote.hpp"
#include "input/script_server.hpp"
#include "ir/ir_tx.hpp"
#include "net/adb.hpp"
#include "net/radio_socket.hpp"
#include "net/wifi.hpp"
#if IR_HUB_WEB_SERVER_ENABLED
#include "net/web_server.hpp"
#endif
#include "output/rgb_led.hpp"

namespace {

constexpr char TAG[] = "app";
constexpr int kBootLedPin = 2;
constexpr uint32_t kBootBlinkMs = 500;
char command[64];
size_t command_length = 0;

void print_tasks() {
    TaskStatus_t status[24] = {};
    const UBaseType_t count = uxTaskGetSystemState(status, 24, nullptr);
    for (UBaseType_t i = 0; i < count; ++i) {
        Serial.printf("%-12s prio=%u free=%u\n", status[i].pcTaskName,
                      static_cast<unsigned>(status[i].uxCurrentPriority),
                      static_cast<unsigned>(status[i].usStackHighWaterMark));
    }
}

void require_ok(esp_err_t err, const char* operation) {
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
        Serial.printf("%s failed: %s\n", operation, esp_err_to_name(err));
        abort();
    }
}

void blink_boot_led() {
    pinMode(kBootLedPin, OUTPUT);
    for (int i = 0; i < 3; ++i) {
        digitalWrite(kBootLedPin, HIGH);
        delay(kBootBlinkMs);
        digitalWrite(kBootLedPin, LOW);
        delay(kBootBlinkMs);
    }
}

void handle_serial_command() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (command_length > 0) {
                command[command_length] = '\0';
                if (std::strncmp(command, "IR ", 3) == 0) {
                    if (!irhub::ir::submit_name(command + 3)) {
                        Serial.println("IR command rejected");
                    }
                } else if (std::strcmp(command, "STATE") == 0) {
                    Serial.printf("state=%s\n",
                                  irhub::control::state_name(irhub::control::current_state()));
                } else if (std::strcmp(command, "BT") == 0) {
                    Serial.printf("ps3=%s\n",
                                  irhub::input::connected() ? "connected" : "disconnected");
                } else if (std::strcmp(command, "BTPAIR") == 0) {
                    const esp_err_t pair_err = irhub::input::start_pairing();
                    Serial.printf("btpair=%s\n", esp_err_to_name(pair_err));
                } else if (std::strncmp(command, "PS3TEST ", 8) == 0) {
                    const char* button = irhub::input::ps3_convert(command + 8);
                    if (!irhub::control::post(button, irhub::control::ButtonSource::Bluetooth)) {
                        Serial.println("ps3test rejected");
                    }
                } else if (std::strcmp(command, "ADB") == 0) {
                    Serial.printf("adb=%s\n",
                                  irhub::adb::connected() ? "connected" : "disconnected");
                } else if (std::strcmp(command, "ADBKEY") == 0) {
                    std::string key;
                    if (irhub::adb::public_key_line(key)) {
                        Serial.printf("adbkey=%s\n", key.c_str());
                        Serial.printf("fingerprint=%s\n",
                                      irhub::adb::public_key_fingerprint().c_str());
                    } else {
                        Serial.println("ADB key unavailable");
                    }
                } else if (std::strncmp(command, "ADB ", 4) == 0) {
                    if (!irhub::adb::submit(command + 4)) {
                        Serial.println("ADB command rejected");
                    }
                } else if (std::strcmp(command, "HEAP") == 0) {
                    Serial.printf("heap=%u min=%u\n",
                                  static_cast<unsigned>(ESP.getFreeHeap()),
                                  static_cast<unsigned>(ESP.getMinFreeHeap()));
                } else if (std::strcmp(command, "TASKS") == 0) {
                    print_tasks();
                } else if (std::strcmp(command, "LATENCY") == 0) {
#if IR_HUB_WEB_SERVER_ENABLED
                    char stats[1024] = {};
                    irhub::web_server::format_stats(stats, sizeof(stats));
                    Serial.printf("%s", stats);
#else
                    Serial.println("web=disabled");
#endif
                } else if (std::strcmp(command, "IP") == 0) {
                    Serial.printf("wifi=%d ip=%s rssi=%d\n", static_cast<int>(WiFi.status()),
                                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
                } else if (!irhub::control::post(command, irhub::control::ButtonSource::Internal)) {
                    Serial.println("control queue full");
                }
                command_length = 0;
            }
        } else if (command_length < sizeof(command) - 1) {
            command[command_length++] = c;
        }
    }
}

}  // namespace

size_t getArduinoLoopTaskStackSize(void) {
    return 4096;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.setDebugOutput(true);
    blink_boot_led();
    esp_log_level_set("*", ESP_LOG_NONE);
    for (const char* tag : {"app", "ps3", "adb", "control", "radio", "web", "ir", "script", "rgb_led"}) {
        esp_log_level_set(tag, ESP_LOG_INFO);
    }

    Serial.println();
    Serial.println("Starting IR hub");
    require_ok(irhub::rgb_led::init(), "rgb_led::init");
    require_ok(irhub::control::init(), "control::init");
    require_ok(irhub::ir::init(), "ir::init");
    require_ok(irhub::radio_socket::init(), "radio_socket::init");
    require_ok(irhub::adb::init(), "adb::init");
    require_ok(irhub::wifi::start(), "wifi::start");
    require_ok(irhub::control::start(), "control::start");
    require_ok(irhub::ir::start(), "ir::start");
    require_ok(irhub::radio_socket::start(), "radio_socket::start");
    require_ok(irhub::adb::start(), "adb::start");
    require_ok(irhub::script_server::start(), "script_server::start");
#if IR_HUB_WEB_SERVER_ENABLED
    require_ok(irhub::web_server::start(), "web_server::start");
#endif
    Serial.println("Services ready; waiting for ADB before Bluetooth");
    for (uint32_t waited = 0; waited < 15000 && !irhub::adb::connected(); waited += 100) {
        delay(100);
    }
    ESP_LOGI(TAG, "ADB ready before PS3 init: %d", irhub::adb::connected() ? 1 : 0);
    ESP_LOGI(TAG, "free heap before PS3 init: %u", static_cast<unsigned>(ESP.getFreeHeap()));
    const esp_err_t ps3_err = irhub::input::init();
    if (ps3_err != ESP_OK) {
        ESP_LOGW(TAG, "PS3 remote init failed: %s", esp_err_to_name(ps3_err));
        Serial.printf("PS3 remote init failed: %s\n", esp_err_to_name(ps3_err));
    }
    ESP_LOGI(TAG, "free heap after init: %u", static_cast<unsigned>(ESP.getFreeHeap()));
    Serial.println("IR hub started");
    Serial.println("Send a button name or 'IR DEVICE COMMAND' followed by newline");
}

void loop() {
    handle_serial_command();
    delay(10);
}
