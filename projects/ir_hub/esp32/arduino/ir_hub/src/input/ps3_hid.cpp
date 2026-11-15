#include "input/ps3_hid.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "config.hpp"
#include "control/control.hpp"
#include "esp32-hal-alloc-bt-classic-mem.h"
#include "esp32-hal-bt.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "input/ps3_hid_compat.hpp"
#include "input/ps3_protocol.hpp"
#include "input/ps3_remote.hpp"

namespace irhub::input {
namespace {

constexpr char TAG[] = "ps3";
constexpr uint64_t kRepeatFirstUs = 700000;
constexpr uint64_t kRepeatNextUs = 200000;
constexpr uint64_t kSleepUs = 1800000ULL * 1000ULL * 1000ULL;
constexpr size_t kButtonLength = 32;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Ps3RepeatTracker s_repeat_tracker;
char s_repeat_button[kButtonLength] = {};
esp_timer_handle_t s_repeat_timer = nullptr;
esp_timer_handle_t s_sleep_timer = nullptr;
std::atomic<bool> s_connected{false};
std::atomic<uint16_t> s_control_cid{0};
std::atomic<uint16_t> s_interrupt_cid{0};
uint8_t s_last_battery = 0;

tL2CAP_CFG_INFO s_config = {};
tL2CAP_APPL_INFO s_callbacks = {};

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void stop_repeat() {
    if (s_repeat_timer != nullptr) {
        esp_timer_stop(s_repeat_timer);
    }
    taskENTER_CRITICAL(&s_mux);
    s_repeat_tracker.release();
    s_repeat_button[0] = '\0';
    taskEXIT_CRITICAL(&s_mux);
}

void reset_sleep_timer() {
    if (s_sleep_timer == nullptr) {
        return;
    }
    esp_timer_stop(s_sleep_timer);
    const esp_err_t err = esp_timer_start_once(s_sleep_timer, kSleepUs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sleep timer start failed: %s", esp_err_to_name(err));
    }
}

void repeat_timer_cb(void*) {
    bool active = false;
    bool due = false;
    char button[kButtonLength] = {};
    taskENTER_CRITICAL(&s_mux);
    active = s_repeat_tracker.active();
    due = active && s_repeat_tracker.tick(now_ms());
    if (due) {
        std::strncpy(button, s_repeat_button, kButtonLength - 1);
        button[kButtonLength - 1] = '\0';
    }
    taskEXIT_CRITICAL(&s_mux);

    if (!active) {
        return;
    }
    if (!due) {
        esp_timer_start_once(s_repeat_timer, 10000);
        return;
    }
    if (!control::post(button, control::ButtonSource::Bluetooth)) {
        ESP_LOGW(TAG, "control queue full for repeat %s", button);
    }
    esp_timer_start_once(s_repeat_timer, kRepeatNextUs);
}

void sleep_timer_cb(void*) {
    if (!s_connected.load()) {
        return;
    }
    ESP_LOGI(TAG, "inactivity timeout; disconnecting remote");
    const uint16_t control_cid = s_control_cid.load();
    const uint16_t interrupt_cid = s_interrupt_cid.load();
    if (control_cid != 0) {
        L2CA_DisconnectReq(control_cid);
    }
    if (interrupt_cid != 0) {
        L2CA_DisconnectReq(interrupt_cid);
    }
}

void handle_report(const Ps3Report& report) {
    if (!report.valid) {
        return;
    }
    if (report.has_battery && report.battery != s_last_battery) {
        s_last_battery = report.battery;
        ESP_LOGI(TAG, "battery level %u", static_cast<unsigned>(report.battery));
        control::set_battery_level(report.battery);
    }
    if (report.release) {
        stop_repeat();
        return;
    }
    if (!report.has_button) {
        return;
    }

    reset_sleep_timer();
    stop_repeat();

    const char* mapped = ps3_map(report.button_value);
    if (mapped == nullptr) {
        ESP_LOGW(TAG, "UNKNOWN scancode %llu",
                 static_cast<unsigned long long>(report.button_value));
        return;
    }
    const char* button = ps3_convert(mapped);
    ESP_LOGI(TAG, "button %s (%s) scancode=%llu", button, mapped,
             static_cast<unsigned long long>(report.button_value));

    if (!control::post(button, control::ButtonSource::Bluetooth)) {
        ESP_LOGW(TAG, "control queue full for %s", button);
        return;
    }

    if (!ps3_is_repeat_key(button)) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    s_repeat_tracker.press(button, now_ms());
    std::strncpy(s_repeat_button, button, kButtonLength - 1);
    s_repeat_button[kButtonLength - 1] = '\0';
    taskEXIT_CRITICAL(&s_mux);
    esp_timer_start_once(s_repeat_timer, kRepeatFirstUs);
}

void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    switch (event) {
        case ESP_BT_GAP_PIN_REQ_EVT: {
            esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
            ESP_LOGI(TAG, "PIN request answered");
            break;
        }
        case ESP_BT_GAP_CFM_REQ_EVT:
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            ESP_LOGI(TAG, "SSP confirmation accepted");
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            ESP_LOGI(TAG, "authentication %s: %s", param->auth_cmpl.device_name,
                     param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? "ok" : "failed");
            break;
        case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(TAG, "link mode changed: %d", param->mode_chg.mode);
            break;
        default:
            break;
    }
}

void l2cap_connect_ind(BD_ADDR bd_addr, UINT16 l2cap_cid, UINT16 psm, UINT8 id) {
    ESP_LOGI(TAG, "L2CAP connect psm=0x%04x cid=0x%04x", psm, l2cap_cid);
    L2CA_ErtmConnectRsp(bd_addr, id, l2cap_cid, kL2capConnectPending,
                        kL2capConnectPendingStatus, nullptr);
    L2CA_ErtmConnectRsp(bd_addr, id, l2cap_cid, kL2capConnectOk,
                        kL2capConnectOk, nullptr);
    L2CA_ConfigReq(l2cap_cid, &s_config);
}

void l2cap_connect_cfm(UINT16 l2cap_cid, UINT16 result) {
    ESP_LOGI(TAG, "L2CAP connected cid=0x%04x result=%u", l2cap_cid, result);
}

void l2cap_config_ind(UINT16 l2cap_cid, tL2CAP_CFG_INFO* config) {
    config->result = kL2capConfigOk;
    L2CA_ConfigRsp(l2cap_cid, config);
    ESP_LOGI(TAG, "L2CAP config request cid=0x%04x mtu=%u", l2cap_cid, config->mtu);
}

void l2cap_config_cfm(UINT16 l2cap_cid, tL2CAP_CFG_INFO* config) {
    ESP_LOGI(TAG, "L2CAP configured cid=0x%04x result=%u", l2cap_cid, config->result);
    if (config->result == kL2capConfigOk) {
        s_connected.store(true);
    }
}

void l2cap_disconnect_ind(UINT16 l2cap_cid, BOOLEAN ack_needed) {
    if (ack_needed) {
        L2CA_DisconnectRsp(l2cap_cid);
    }
    ESP_LOGI(TAG, "L2CAP disconnected cid=0x%04x", l2cap_cid);
    if (s_control_cid.load() == l2cap_cid) {
        s_control_cid.store(0);
    }
    if (s_interrupt_cid.load() == l2cap_cid) {
        s_interrupt_cid.store(0);
    }
    s_connected.store(false);
    stop_repeat();
    if (s_sleep_timer != nullptr) {
        esp_timer_stop(s_sleep_timer);
    }
}

void l2cap_disconnect_cfm(UINT16 l2cap_cid, UINT16 result) {
    ESP_LOGI(TAG, "L2CAP disconnect confirmed cid=0x%04x result=%u", l2cap_cid, result);
}

void l2cap_data_ind(UINT16 l2cap_cid, BT_HDR* packet) {
    if (packet == nullptr) {
        return;
    }
    const uint8_t* base = packet->data;
    const size_t length = packet->len;
    ESP_LOGI(TAG, "HID data cid=0x%04x len=%u offset=%u first=%02X %02X %02X %02X",
             l2cap_cid, static_cast<unsigned>(length), packet->offset,
             length > 0 ? base[0] : 0, length > 1 ? base[1] : 0,
             length > 2 ? base[2] : 0, length > 3 ? base[3] : 0);

    Ps3Report report = parse_ps3_bd_report(base, length);
    if (!report.valid && packet->offset > 0 && packet->offset < length) {
        report = parse_ps3_bd_report(base + packet->offset, length - packet->offset);
    }
    handle_report(report);
    std::free(packet);
}

void l2cap_congest(UINT16 l2cap_cid, BOOLEAN congested) {
    ESP_LOGW(TAG, "L2CAP congestion cid=0x%04x congested=%d", l2cap_cid, congested);
}

UINT16 register_l2cap_service(const char* name, UINT16 psm, UINT8 service_id) {
    const UINT16 cid = L2CA_Register(psm, &s_callbacks);
    if (cid == 0) {
        ESP_LOGE(TAG, "L2CAP register failed for %s", name);
        return 0;
    }
    if (!BTM_SetSecurityLevel(false, name, service_id, kSecNone, psm, 0, 0)) {
        ESP_LOGW(TAG, "security registration failed for %s", name);
    }
    ESP_LOGI(TAG, "%s ready on psm=0x%04x cid=0x%04x", name, psm, cid);
    return cid;
}

esp_err_t start_bluetooth() {
    if (!btStarted()) {
        if (!btMemRelease(BT_MODE_BLE)) {
            return ESP_FAIL;
        }
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        cfg.mode = ESP_BT_MODE_CLASSIC_BT;
        cfg.bt_max_acl_conn = 1;
        cfg.ble_max_conn = 0;
        cfg.normal_adv_size = 10;
        cfg.mesh_adv_size = 10;
        ESP_LOGI(TAG, "free heap before controller: %u",
                 static_cast<unsigned>(esp_get_free_heap_size()));
        esp_err_t controller_err = esp_bt_controller_init(&cfg);
        if (controller_err != ESP_OK) {
            return controller_err;
        }
        controller_err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        if (controller_err != ESP_OK) {
            return controller_err;
        }
        ESP_LOGI(TAG, "free heap after controller: %u",
                 static_cast<unsigned>(esp_get_free_heap_size()));
    }

    esp_err_t err = ESP_OK;
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        err = esp_bluedroid_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        err = esp_bluedroid_enable();
        if (err != ESP_OK) {
            return err;
        }
    }

    err = esp_bt_gap_register_callback(gap_callback);
    if (err != ESP_OK) {
        return err;
    }
    esp_bt_gap_set_device_name("IR Hub");

    esp_bt_io_cap_t io_cap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(io_cap));
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin);

    s_callbacks.pL2CA_ConnectInd_Cb = l2cap_connect_ind;
    s_callbacks.pL2CA_ConnectCfm_Cb = l2cap_connect_cfm;
    s_callbacks.pL2CA_ConnectPnd_Cb = nullptr;
    s_callbacks.pL2CA_ConfigInd_Cb = l2cap_config_ind;
    s_callbacks.pL2CA_ConfigCfm_Cb = l2cap_config_cfm;
    s_callbacks.pL2CA_DisconnectInd_Cb = l2cap_disconnect_ind;
    s_callbacks.pL2CA_DisconnectCfm_Cb = l2cap_disconnect_cfm;
    s_callbacks.pL2CA_QoSViolationInd_Cb = nullptr;
    s_callbacks.pL2CA_DataInd_Cb = l2cap_data_ind;
    s_callbacks.pL2CA_CongestionStatus_Cb = l2cap_congest;
    s_callbacks.pL2CA_TxComplete_Cb = nullptr;

    s_control_cid.store(register_l2cap_service("PS3-HIDC", kL2capPsmsHidControl,
                                               kSecServiceFirstEmpty));
    s_interrupt_cid.store(register_l2cap_service("PS3-HIDI", kL2capPsmsHidInterrupt,
                                                  kSecServiceFirstEmpty + 1));

    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

}  // namespace

esp_err_t init() {
    esp_timer_create_args_t repeat_args = {};
    repeat_args.callback = repeat_timer_cb;
    repeat_args.name = "ps3_repeat";
    esp_err_t timer_err = esp_timer_create(&repeat_args, &s_repeat_timer);
    if (timer_err != ESP_OK) {
        return timer_err;
    }

    esp_timer_create_args_t sleep_args = {};
    sleep_args.callback = sleep_timer_cb;
    sleep_args.name = "ps3_sleep";
    timer_err = esp_timer_create(&sleep_args, &s_sleep_timer);
    if (timer_err != ESP_OK) {
        return timer_err;
    }

    const esp_err_t err = start_bluetooth();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HID host ready; hold Start+Enter on the remote to pair");
    return ESP_OK;
}

bool connected() {
    return s_connected.load();
}

}  // namespace irhub::input
