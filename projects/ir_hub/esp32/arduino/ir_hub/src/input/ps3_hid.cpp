#include "input/ps3_hid.hpp"

#include <Arduino.h>

#include <atomic>
#include <cstdio>
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
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input/ps3_hid_compat.hpp"
#include "input/ps3_protocol.hpp"
#include "input/ps3_remote.hpp"

namespace irhub::input {
namespace {

constexpr char TAG[] = "ps3";
constexpr uint64_t kRepeatFirstUs = 700000;
constexpr uint64_t kRepeatNextUs = 200000;
constexpr uint64_t kSleepUs = 1800000ULL * 1000ULL * 1000ULL;
constexpr uint64_t kDiscoveryRetryUs = 30000000;
constexpr uint64_t kConnectTimeoutUs = 10000000;
constexpr uint32_t kPairingWindowMs = 60000U;
constexpr uint64_t kPairingInquiryRetryUs = 6000000ULL;
constexpr uint8_t kInquiryLength = 0x04;
constexpr uint32_t kRemoteCod = 0x0000250C;
constexpr size_t kButtonLength = 32;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_bda_mux = portMUX_INITIALIZER_UNLOCKED;
Ps3RepeatTracker s_repeat_tracker;
char s_repeat_button[kButtonLength] = {};
esp_timer_handle_t s_repeat_timer = nullptr;
esp_timer_handle_t s_sleep_timer = nullptr;
esp_timer_handle_t s_discovery_timer = nullptr;
std::atomic<bool> s_connected{false};
std::atomic<bool> s_discovering{false};
std::atomic<bool> s_connecting{false};
std::atomic<bool> s_connect_pending{false};
std::atomic<bool> s_sleeping{false};
std::atomic<uint32_t> s_pairing_until_ms{0};
std::atomic<uint16_t> s_control_cid{0};
std::atomic<uint16_t> s_interrupt_cid{0};
uint8_t s_remote_bda[6] = {};
uint8_t s_known_bda[6] = {};
bool s_has_known_bda = false;
uint8_t s_last_battery = 0;

enum class BtWork : uint8_t { Connect, StartDiscovery, FinishPairing };
struct BtWorkItem {
    BtWork type;
    uint8_t bda[6];
};
QueueHandle_t s_bt_work_queue = nullptr;

void enqueue_work(BtWork type, const uint8_t* bda = nullptr) {
    if (s_bt_work_queue == nullptr) {
        return;
    }
    BtWorkItem item = {};
    item.type = type;
    if (bda != nullptr) {
        std::memcpy(item.bda, bda, sizeof(item.bda));
    }
    if (xQueueSend(s_bt_work_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "BT work queue full");
    }
}

void enqueue_connect(const uint8_t* bda) {
    enqueue_work(BtWork::Connect, bda);
}

void enqueue_start_discovery() {
    enqueue_work(BtWork::StartDiscovery);
}

void enqueue_finish_pairing() {
    enqueue_work(BtWork::FinishPairing);
}

tL2CAP_CFG_INFO s_config = {};
tL2CAP_APPL_INFO s_callbacks = {};

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool pairing_active() {
    const uint32_t deadline = s_pairing_until_ms.load();
    return deadline != 0 && static_cast<int32_t>(now_ms() - deadline) < 0;
}

esp_err_t finish_pairing() {
    if (s_pairing_until_ms.exchange(0) == 0) {
        return ESP_OK;
    }
    const esp_err_t err =
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    ESP_LOGI(TAG, "pairing window closed");
    return err;
}

void format_bda(const uint8_t* bda, char* out, size_t length) {
    std::snprintf(out, length, "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1],
                  bda[2], bda[3], bda[4], bda[5]);
}

bool is_remote_name(const char* name) {
    return name != nullptr && std::strstr(name, "BD Remote") != nullptr;
}

void extract_eir_name(const uint8_t* eir, int length, char* name, size_t capacity) {
    if (eir == nullptr || length <= 0 || name[0] != '\0') {
        return;
    }
    int offset = 0;
    while (offset + 1 < length) {
        const uint8_t field_length = eir[offset];
        if (field_length == 0 || offset + 1 + field_length > length) {
            break;
        }
        const uint8_t type = eir[offset + 1];
        if (type == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME ||
            type == ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME) {
            size_t copy_length = field_length - 1;
            if (copy_length >= capacity) {
                copy_length = capacity - 1;
            }
            std::memcpy(name, eir + offset + 2, copy_length);
            name[copy_length] = '\0';
            return;
        }
        offset += field_length + 1;
    }
}

void schedule_discovery(uint64_t delay_us) {
    if (s_discovery_timer == nullptr) {
        return;
    }
    esp_timer_stop(s_discovery_timer);
    const esp_err_t err = esp_timer_start_once(s_discovery_timer, delay_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "discovery timer start failed: %s", esp_err_to_name(err));
    }
}

void start_discovery() {
    if (s_connected.load() || s_connecting.load() || s_connect_pending.load() ||
        s_discovering.load()) {
        return;
    }
    const esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                                     kInquiryLength, 0);
    if (err == ESP_OK) {
        s_discovering.store(true);
        ESP_LOGI(TAG, "inquiry started; hold Start+Enter for 5s on the BD remote");
    } else {
        ESP_LOGW(TAG, "inquiry start failed: %s", esp_err_to_name(err));
        schedule_discovery(kDiscoveryRetryUs);
    }
}

void connect_remote(const uint8_t* bda);
void connect_known_remote();

void discovery_timer_cb(void*) {
    if (pairing_active()) {
        if (!s_connected.load() && !s_connecting.load() && !s_discovering.load()) {
            enqueue_start_discovery();
        }
        schedule_discovery(kPairingInquiryRetryUs);
        return;
    }
    enqueue_finish_pairing();
    if (s_connected.load()) {
        return;
    }
    if (s_connecting.load()) {
        ESP_LOGW(TAG, "connection attempt timed out");
        s_connecting.store(false);
        s_control_cid.store(0);
        s_interrupt_cid.store(0);
    }
    connect_known_remote();
}

bool parse_known_bda() {
    const char* text = config::kPs3RemoteMac;
    uint8_t bytes[6] = {};
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        const char* begin = text + i * 3;
        if (begin[0] == '\0' || begin[1] == '\0') {
            return false;
        }
        char* end = nullptr;
        const unsigned long value = std::strtoul(begin, &end, 16);
        if (end != begin + 2 || value > 255U) {
            return false;
        }
        bytes[i] = static_cast<uint8_t>(value);
        if (i + 1 < sizeof(bytes) && begin[2] != ':') {
            return false;
        }
    }
    std::memcpy(s_known_bda, bytes, sizeof(bytes));
    s_has_known_bda = true;
    return true;
}

void connect_known_remote() {
    if (s_has_known_bda) {
        connect_remote(s_known_bda);
        return;
    }
    start_discovery();
}

void schedule_reconnect() {
    schedule_discovery(kDiscoveryRetryUs);
}

void connect_remote(const uint8_t* bda) {
    if (s_connected.load() || s_connecting.exchange(true)) {
        return;
    }

    taskENTER_CRITICAL(&s_bda_mux);
    std::memcpy(s_remote_bda, bda, sizeof(s_remote_bda));
    taskEXIT_CRITICAL(&s_bda_mux);

    char address[18] = {};
    format_bda(s_remote_bda, address, sizeof(address));
    const UINT16 cid = L2CA_ConnectReq(kL2capPsmsHidInterrupt, s_remote_bda);
    if (cid == 0) {
        ESP_LOGW(TAG, "HID interrupt connect request to %s failed", address);
        s_connecting.store(false);
        schedule_reconnect();
        return;
    }
    s_interrupt_cid.store(cid);
    ESP_LOGI(TAG, "connecting to %s; HID interrupt cid=0x%04x", address, cid);
    schedule_discovery(kConnectTimeoutUs);
}

void bt_work_task(void*) {
    BtWorkItem item = {};
    while (xQueueReceive(s_bt_work_queue, &item, portMAX_DELAY) == pdTRUE) {
        if (item.type == BtWork::Connect) {
            connect_remote(item.bda);
        } else if (item.type == BtWork::StartDiscovery) {
            start_discovery();
        } else if (item.type == BtWork::FinishPairing) {
            finish_pairing();
        }
    }
}

void handle_discovery_result(const esp_bt_gap_cb_param_t::disc_res_param& result) {
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {};
    uint32_t cod = 0;

    for (int i = 0; i < result.num_prop; ++i) {
        const esp_bt_gap_dev_prop_t& prop = result.prop[i];
        if (prop.val == nullptr || prop.len <= 0) {
            continue;
        }
        if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            size_t copy_length = static_cast<size_t>(prop.len);
            if (copy_length >= sizeof(name)) {
                copy_length = sizeof(name) - 1;
            }
            std::memcpy(name, prop.val, copy_length);
            name[copy_length] = '\0';
        } else if (prop.type == ESP_BT_GAP_DEV_PROP_COD &&
                   prop.len >= static_cast<int>(sizeof(cod))) {
            std::memcpy(&cod, prop.val, sizeof(cod));
        } else if (prop.type == ESP_BT_GAP_DEV_PROP_EIR) {
            extract_eir_name(static_cast<const uint8_t*>(prop.val), prop.len, name,
                             sizeof(name));
        }
    }

    char address[18] = {};
    format_bda(result.bda, address, sizeof(address));
    if (name[0] != '\0' || cod != 0) {
        ESP_LOGI(TAG, "found %s [%s] cod=0x%06X", name[0] != '\0' ? name : "?",
                 address, static_cast<unsigned>(cod));
    }

    if (s_connected.load() || s_connecting.load() || s_connect_pending.load()) {
        return;
    }
    if (is_remote_name(name) || cod == kRemoteCod) {
        ESP_LOGI(TAG, "BD remote found at %s", address);
        taskENTER_CRITICAL(&s_bda_mux);
        std::memcpy(s_remote_bda, result.bda, sizeof(s_remote_bda));
        taskEXIT_CRITICAL(&s_bda_mux);
        s_connect_pending.store(true);
        if (esp_bt_gap_cancel_discovery() != ESP_OK) {
            s_connect_pending.store(false);
            enqueue_connect(result.bda);
        }
    }
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
    s_sleeping.store(false);
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
    Serial.printf("PS3 repeat: %s\n", button);
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
    s_sleeping.store(true);
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
    ESP_LOGI(TAG,
             "report valid=%d has_button=%d release=%d has_battery=%d scancode=%llu",
             report.valid ? 1 : 0, report.has_button ? 1 : 0, report.release ? 1 : 0,
             report.has_battery ? 1 : 0,
             static_cast<unsigned long long>(report.button_value));
    if (!report.valid) {
        return;
    }
    if (report.has_battery && report.battery != s_last_battery) {
        s_last_battery = report.battery;
        ESP_LOGI(TAG, "battery level %u", static_cast<unsigned>(report.battery));
        control::set_battery_level(report.battery);
    }
    if (report.release) {
        Serial.println("PS3 release");
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
        Serial.printf("PS3 unknown scancode=%llu\n",
                      static_cast<unsigned long long>(report.button_value));
        ESP_LOGW(TAG, "UNKNOWN scancode %llu",
                 static_cast<unsigned long long>(report.button_value));
        return;
    }
    const char* button = ps3_convert(mapped);
    ESP_LOGI(TAG, "button %s (%s) scancode=%llu", button, mapped,
             static_cast<unsigned long long>(report.button_value));
    Serial.printf("PS3 key: %s (%s, scancode=%llu)\n", button, mapped,
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
        case ESP_BT_GAP_DISC_RES_EVT:
            handle_discovery_result(param->disc_res);
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            const bool started =
                param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED;
            s_discovering.store(started);
            ESP_LOGI(TAG, "inquiry %s", started ? "started" : "stopped");
            if (!started) {
                if (s_connect_pending.exchange(false)) {
                    uint8_t bda[6] = {};
                    taskENTER_CRITICAL(&s_bda_mux);
                    std::memcpy(bda, s_remote_bda, sizeof(bda));
                    taskEXIT_CRITICAL(&s_bda_mux);
                    enqueue_connect(bda);
                } else if (!pairing_active() && !s_connected.load() && !s_connecting.load()) {
                    schedule_reconnect();
                }
            }
            break;
        }
        case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
            ESP_LOGI(TAG, "remote name '%s' status=%d",
                     param->read_rmt_name.rmt_name,
                     static_cast<int>(param->read_rmt_name.stat));
            break;
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
            ESP_LOGI(TAG, "ACL connected handle=%u status=%d",
                     static_cast<unsigned>(param->acl_conn_cmpl_stat.handle),
                     static_cast<int>(param->acl_conn_cmpl_stat.stat));
            break;
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            ESP_LOGI(TAG, "ACL disconnected handle=%u reason=%d",
                     static_cast<unsigned>(param->acl_disconn_cmpl_stat.handle),
                     static_cast<int>(param->acl_disconn_cmpl_stat.reason));
            break;
        default:
            break;
    }
}

void l2cap_connect_ind(BD_ADDR bd_addr, UINT16 l2cap_cid, UINT16 psm, UINT8 id) {
    ESP_LOGI(TAG, "L2CAP connect psm=0x%04x cid=0x%04x", psm, l2cap_cid);
    if (psm == kL2capPsmsHidControl) {
        s_control_cid.store(l2cap_cid);
    } else if (psm == kL2capPsmsHidInterrupt) {
        s_interrupt_cid.store(l2cap_cid);
    }
    L2CA_ErtmConnectRsp(bd_addr, id, l2cap_cid, kL2capConnectPending,
                        kL2capConnectPendingStatus, nullptr);
    L2CA_ErtmConnectRsp(bd_addr, id, l2cap_cid, kL2capConnectOk,
                        kL2capConnectOk, nullptr);
    L2CA_ConfigReq(l2cap_cid, &s_config);
}

void l2cap_connect_cfm(UINT16 l2cap_cid, UINT16 result) {
    ESP_LOGI(TAG, "L2CAP connected cid=0x%04x result=%u", l2cap_cid, result);
    if (result != kL2capConnectOk) {
        if (s_connecting.load()) {
            s_connecting.store(false);
            s_control_cid.store(0);
            s_interrupt_cid.store(0);
            schedule_reconnect();
        }
        return;
    }
    if (s_connecting.load()) {
        L2CA_ConfigReq(l2cap_cid, &s_config);
    }
}

void l2cap_config_ind(UINT16 l2cap_cid, tL2CAP_CFG_INFO* config) {
    config->result = kL2capConfigOk;
    L2CA_ConfigRsp(l2cap_cid, config);
    ESP_LOGI(TAG, "L2CAP config request cid=0x%04x mtu=%u", l2cap_cid, config->mtu);
}

void l2cap_config_cfm(UINT16 l2cap_cid, tL2CAP_CFG_INFO* config) {
    ESP_LOGI(TAG, "L2CAP configured cid=0x%04x result=%u", l2cap_cid, config->result);
    if (config->result != kL2capConfigOk) {
        return;
    }
    const uint16_t interrupt_cid = s_interrupt_cid.load();
    if (interrupt_cid != 0 && l2cap_cid == interrupt_cid && !s_connected.exchange(true)) {
        s_connecting.store(false);
        s_discovering.store(false);
        s_sleeping.store(false);
        if (s_discovery_timer != nullptr) {
            esp_timer_stop(s_discovery_timer);
        }
        reset_sleep_timer();
        enqueue_finish_pairing();
        ESP_LOGI(TAG, "BD remote ready");
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
    s_connecting.store(false);
    s_connect_pending.store(false);
    stop_repeat();
    if (s_sleep_timer != nullptr) {
        esp_timer_stop(s_sleep_timer);
    }
    const bool was_sleeping = s_sleeping.exchange(false);
    if (!was_sleeping) {
        schedule_discovery(kDiscoveryRetryUs);
    }
}

void l2cap_disconnect_cfm(UINT16 l2cap_cid, UINT16 result) {
    ESP_LOGI(TAG, "L2CAP disconnect confirmed cid=0x%04x result=%u", l2cap_cid, result);
}

void l2cap_data_ind(UINT16 l2cap_cid, BT_HDR* packet) {
    if (packet == nullptr) {
        return;
    }
    const uint8_t* payload = packet->data + packet->offset;
    const size_t length = packet->len;
    ESP_LOGI(TAG, "HID data cid=0x%04x len=%u offset=%u first=%02X %02X %02X %02X",
             l2cap_cid, static_cast<unsigned>(length), packet->offset,
             length > 0 ? payload[0] : 0, length > 1 ? payload[1] : 0,
             length > 2 ? payload[2] : 0, length > 3 ? payload[3] : 0);

    handle_report(parse_ps3_bd_report(payload, length));
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

    std::memset(&s_config, 0, sizeof(s_config));
    s_config.mtu_present = true;
    s_config.mtu = 64;

    s_control_cid.store(register_l2cap_service("PS3-HIDC", kL2capPsmsHidControl,
                                               kSecServiceFirstEmpty));
    s_interrupt_cid.store(register_l2cap_service("PS3-HIDI", kL2capPsmsHidInterrupt,
                                                  kSecServiceFirstEmpty + 1));

    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

}  // namespace

esp_err_t init() {
    s_bt_work_queue = xQueueCreate(4, sizeof(BtWorkItem));
    if (s_bt_work_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(bt_work_task, "bt_work", 2048, nullptr, 2, nullptr,
                                config::kAppCore) != pdPASS) {
        vQueueDelete(s_bt_work_queue);
        s_bt_work_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

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

    esp_timer_create_args_t discovery_args = {};
    discovery_args.callback = discovery_timer_cb;
    discovery_args.name = "ps3_scan";
    timer_err = esp_timer_create(&discovery_args, &s_discovery_timer);
    if (timer_err != ESP_OK) {
        return timer_err;
    }

    const esp_err_t err = start_bluetooth();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth start failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!parse_known_bda()) {
        ESP_LOGW(TAG, "invalid configured remote MAC; falling back to inquiry");
    }
    ESP_LOGI(TAG, "HID host ready; waiting for remote-initiated connection");
    return ESP_OK;
}

esp_err_t start_pairing() {
    if (s_connected.load()) {
        ESP_LOGI(TAG, "remote already connected");
        return ESP_OK;
    }
    const esp_err_t mode_err =
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (mode_err != ESP_OK) {
        return mode_err;
    }
    s_pairing_until_ms.store(now_ms() + kPairingWindowMs);
    s_connecting.store(false);
    s_discovering.store(false);
    start_discovery();
    schedule_discovery(kPairingInquiryRetryUs);
    ESP_LOGI(TAG, "pairing window open for %u s; hold Start+Enter on the remote",
             static_cast<unsigned>(kPairingWindowMs / 1000U));
    return ESP_OK;
}

bool connected() {
    return s_connected.load();
}

}  // namespace irhub::input
