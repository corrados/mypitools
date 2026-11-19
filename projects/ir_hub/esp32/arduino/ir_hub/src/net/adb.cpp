#include "net/adb.hpp"

#include <Arduino.h>
#include <WiFi.h>

#include <Preferences.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "config.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/bignum.h"
#include "mbedtls/md.h"
#include "mbedtls/md5.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "net/adb_keys.hpp"

namespace irhub::adb {
namespace {

constexpr char TAG[] = "adb";
constexpr uint16_t kAdbPort = 5555;
constexpr uint32_t kProtocolVersion = 0x01000000U;
constexpr uint32_t kMaxPayload = 1024U;
constexpr uint32_t kReconnectMs = 5000U;
constexpr size_t kMaxInFlight = 8;
constexpr uint32_t kAuthWaitMs = 60000U;
constexpr uint32_t kAuthRetryDelayMs = 30000U;
constexpr uint32_t kConnectTimeoutMs = 2000U;
constexpr uint32_t kPacketTimeoutMs = 1000U;
constexpr size_t kShellTailLength = 64;
constexpr uint32_t kLocalId = 1U;
constexpr uint32_t kAuthToken = 1U;
constexpr uint32_t kAuthSignature = 2U;
constexpr uint32_t kAuthPublicKey = 3U;
constexpr uint32_t kCmdCnxn = 0x4e584e43U;
constexpr uint32_t kCmdAuth = 0x48545541U;
constexpr uint32_t kCmdOpen = 0x4e45504fU;
constexpr uint32_t kCmdOkay = 0x59414b4fU;
constexpr uint32_t kCmdWrte = 0x45545257U;
constexpr uint32_t kCmdClse = 0x45534c43U;
constexpr size_t kQueueLength = 64;
constexpr size_t kSendBatch = 4;
constexpr size_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 2;
#ifdef IR_HUB_HAS_ADB_KEY_SECRET
constexpr const char* kAdbUser = adb_secret::kAdbKeyUser;
#else
constexpr const char* kAdbUser = " ir-hub@esp32";
#endif

struct Packet {
    uint32_t command = 0;
    uint32_t arg0 = 0;
    uint32_t arg1 = 0;
    const uint8_t* data = nullptr;
    size_t length = 0;
};

uint8_t s_packet_buffer[kMaxPayload] = {};

struct AdbItem {
    char key[32];
    uint32_t enqueued_us = 0;
};

QueueHandle_t s_queue = nullptr;
WiFiClient s_client;
mbedtls_pk_context s_key = {};
bool s_key_ready = false;
bool s_signature_sent = false;
bool s_shell_ready = false;
char s_shell_tail[kShellTailLength + 1] = {};
size_t s_shell_tail_length = 0;
uint32_t s_sent_us[kQueueLength] = {};
size_t s_sent_head = 0;
size_t s_sent_tail = 0;
uint32_t s_remote_id = 0;
uint32_t s_last_packet_ms = 0;
std::atomic<bool> s_connected{false};
uint32_t s_reconnect_delay_ms = kReconnectMs;
uint32_t s_auth_wait_deadline_ms = 0;
AdbStats s_stats;

void clear_sent_times() {
    s_sent_head = 0;
    s_sent_tail = 0;
}

size_t in_flight_count() {
    return (s_sent_tail + kQueueLength - s_sent_head) % kQueueLength;
}

void push_sent_time(uint32_t sent_us) {
    const size_t next = (s_sent_tail + 1) % kQueueLength;
    if (next == s_sent_head) {
        s_sent_head = (s_sent_head + 1) % kQueueLength;
    }
    s_sent_us[s_sent_tail] = sent_us;
    s_sent_tail = next;
}

uint32_t pop_sent_time() {
    if (s_sent_head == s_sent_tail) {
        return 0;
    }
    const uint32_t sent_us = s_sent_us[s_sent_head];
    s_sent_head = (s_sent_head + 1) % kQueueLength;
    return sent_us;
}

int rng(void*, unsigned char* buffer, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<unsigned char>(esp_random());
    }
    return 0;
}

void write_le32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t read_le32(const uint8_t* in) {
    return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return sum;
}

bool write_all(const uint8_t* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        if (!s_client.connected()) {
            return false;
        }
        const size_t written = s_client.write(data + offset, length - offset);
        if (written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool send_packet(uint32_t command, uint32_t arg0, uint32_t arg1, const uint8_t* data,
                 size_t length) {
    uint8_t header[24] = {};
    write_le32(header, command);
    write_le32(header + 4, arg0);
    write_le32(header + 8, arg1);
    write_le32(header + 12, static_cast<uint32_t>(length));
    write_le32(header + 16, checksum(data, length));
    write_le32(header + 20, command ^ 0xffffffffU);
    if (!write_all(header, sizeof(header))) {
        return false;
    }
    return length == 0 || write_all(data, length);
}

bool read_packet(Packet& packet) {
    uint8_t header[24] = {};
    s_client.setTimeout(kPacketTimeoutMs);
    if (s_client.readBytes(header, sizeof(header)) != sizeof(header)) {
        return false;
    }

    packet.command = read_le32(header);
    packet.arg0 = read_le32(header + 4);
    packet.arg1 = read_le32(header + 8);
    const uint32_t length = read_le32(header + 12);
    const uint32_t expected_checksum = read_le32(header + 16);
    const uint32_t magic = read_le32(header + 20);
    if (magic != (packet.command ^ 0xffffffffU) || length > kMaxPayload) {
        return false;
    }

    packet.data = s_packet_buffer;
    packet.length = length;
    if (length > 0 && s_client.readBytes(s_packet_buffer, length) != length) {
        return false;
    }
    return checksum(packet.data, length) == expected_checksum;
}

bool ensure_key() {
    if (s_key_ready) {
        return true;
    }

    mbedtls_pk_init(&s_key);
    Preferences preferences;
    preferences.begin("adb", false);
    size_t stored_length = preferences.getBytesLength("rsa_der");
    std::vector<uint8_t> der(stored_length);
    if (stored_length > 0) {
        preferences.getBytes("rsa_der", der.data(), der.size());
    }
#ifdef IR_HUB_HAS_ADB_KEY_SECRET
    const bool needs_secret_key =
        stored_length != adb_secret::kAdbKeyDerLength ||
        std::memcmp(der.data(), adb_secret::kAdbKeyDer, adb_secret::kAdbKeyDerLength) != 0;
    if (needs_secret_key) {
        const size_t written = preferences.putBytes("rsa_der", adb_secret::kAdbKeyDer,
                                                    adb_secret::kAdbKeyDerLength);
        preferences.end();
        if (written != adb_secret::kAdbKeyDerLength) {
            ESP_LOGE(TAG, "cannot store pre-authorized ADB key");
            mbedtls_pk_free(&s_key);
            return false;
        }
        der.assign(adb_secret::kAdbKeyDer,
                   adb_secret::kAdbKeyDer + adb_secret::kAdbKeyDerLength);
        stored_length = adb_secret::kAdbKeyDerLength;
        preferences.begin("adb", false);
        ESP_LOGI(TAG, "ADB key replaced with pre-authorized key (%u bytes)",
                 static_cast<unsigned>(stored_length));
    }
#endif
    preferences.end();

    int result = 0;
    if (stored_length > 0) {
        result = mbedtls_pk_parse_key(&s_key, der.data(), der.size(), nullptr, 0, rng, nullptr);
        if (result == 0) {
            ESP_LOGI(TAG, "ADB key loaded from NVS (%u bytes)",
                     static_cast<unsigned>(stored_length));
        }
    } else {
        const mbedtls_pk_info_t* info = mbedtls_pk_info_from_type(MBEDTLS_PK_RSA);
        if (info == nullptr || mbedtls_pk_setup(&s_key, info) != 0) {
            ESP_LOGE(TAG, "cannot set up RSA key");
            mbedtls_pk_free(&s_key);
            return false;
        }
        result = mbedtls_rsa_gen_key(mbedtls_pk_rsa(s_key), rng, nullptr, 2048, 65537);
        if (result == 0) {
            uint8_t buffer[2048] = {};
            const int der_length = mbedtls_pk_write_key_der(&s_key, buffer, sizeof(buffer));
            if (der_length <= 0) {
                result = -1;
            } else {
                preferences.begin("adb", false);
                const size_t written =
                    preferences.putBytes("rsa_der", buffer + sizeof(buffer) - der_length,
                                         static_cast<size_t>(der_length));
                preferences.end();
                if (written != static_cast<size_t>(der_length)) {
                    result = -1;
                } else {
                    ESP_LOGI(TAG, "ADB key generated and stored (%d bytes)", der_length);
                }
            }
        }
    }

    if (result != 0) {
        ESP_LOGE(TAG, "ADB key failure: -0x%04x", static_cast<unsigned>(-result));
        mbedtls_pk_free(&s_key);
        return false;
    }

    s_key_ready = true;
    ESP_LOGI(TAG, "RSA key ready");
    return true;
}

bool sign_token(const uint8_t* token, size_t length, std::vector<uint8_t>& signature) {
    if (length != 20) {
        return false;
    }
    signature.resize(256);
    size_t signature_length = 0;
    const int result = mbedtls_pk_sign(&s_key, MBEDTLS_MD_SHA1, token, length, signature.data(),
                                       signature.size(), &signature_length, rng, nullptr);
    if (result != 0) {
        ESP_LOGE(TAG, "token signing failed: -0x%04x", static_cast<unsigned>(-result));
        return false;
    }
    signature.resize(signature_length);
    return true;
}

std::string base64(const uint8_t* data, size_t length) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((length + 2) / 3) * 4);
    for (size_t i = 0; i < length; i += 3) {
        const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
                               ((i + 1 < length ? static_cast<uint32_t>(data[i + 1]) : 0) << 8) |
                               (i + 2 < length ? static_cast<uint32_t>(data[i + 2]) : 0);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
        out.push_back(i + 1 < length ? kAlphabet[(chunk >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < length ? kAlphabet[chunk & 0x3f] : '=');
    }
    return out;
}

bool public_key(std::string& out) {
    mbedtls_mpi n = {};
    mbedtls_mpi e = {};
    mbedtls_mpi r32 = {};
    mbedtls_mpi n_mod = {};
    mbedtls_mpi inverse = {};
    mbedtls_mpi n0inv_value = {};
    mbedtls_mpi rr = {};
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&r32);
    mbedtls_mpi_init(&n_mod);
    mbedtls_mpi_init(&inverse);
    mbedtls_mpi_init(&n0inv_value);
    mbedtls_mpi_init(&rr);

    bool ok = mbedtls_rsa_export(mbedtls_pk_rsa(s_key), &n, nullptr, nullptr, nullptr, &e) == 0;
    uint8_t blob[524] = {};
    if (ok) {
        write_le32(blob, 64);
        ok = mbedtls_mpi_set_bit(&r32, 32, 1) == 0 &&
             mbedtls_mpi_mod_mpi(&n_mod, &n, &r32) == 0 &&
             mbedtls_mpi_inv_mod(&inverse, &n_mod, &r32) == 0 &&
             mbedtls_mpi_sub_mpi(&n0inv_value, &r32, &inverse) == 0 &&
             mbedtls_mpi_write_binary_le(&n0inv_value, blob + 4, 4) == 0 &&
             mbedtls_mpi_write_binary_le(&n, blob + 8, 256) == 0 &&
             mbedtls_mpi_set_bit(&rr, 2048, 1) == 0 &&
             mbedtls_mpi_mul_mpi(&rr, &rr, &rr) == 0 &&
             mbedtls_mpi_mod_mpi(&rr, &rr, &n) == 0 &&
             mbedtls_mpi_write_binary_le(&rr, blob + 264, 256) == 0 &&
             mbedtls_mpi_write_binary_le(&e, blob + 520, 4) == 0;
    }
    if (ok) {
        out = base64(blob, sizeof(blob));
        out += kAdbUser;
    }

    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&r32);
    mbedtls_mpi_free(&n_mod);
    mbedtls_mpi_free(&inverse);
    mbedtls_mpi_free(&n0inv_value);
    mbedtls_mpi_free(&rr);
    return ok;
}

void reset_shell_output() {
    s_shell_tail_length = 0;
    s_shell_tail[0] = '\0';
}

void observe_shell_output(const Packet& packet) {
    if (packet.length == 0) {
        return;
    }
    char combined[kShellTailLength + 128] = {};
    size_t length = 0;
    if (s_shell_tail_length > 0) {
        std::memcpy(combined, s_shell_tail, s_shell_tail_length);
        length = s_shell_tail_length;
    }
    const size_t available = sizeof(combined) - length - 1;
    const size_t received = packet.length < available ? packet.length : available;
    std::memcpy(combined + length, packet.data, received);
    length += received;
    combined[length] = '\0';

    const size_t tail_length = length < kShellTailLength ? length : kShellTailLength;
    std::memcpy(s_shell_tail, combined + length - tail_length, tail_length);
    s_shell_tail[tail_length] = '\0';
    s_shell_tail_length = tail_length;
}

void disconnect(const char* reason) {
    ESP_LOGI(TAG, "disconnected: %s", reason);
    s_connected.store(false);
    s_shell_ready = false;
    clear_sent_times();
    reset_shell_output();
    s_signature_sent = false;
    s_remote_id = 0;
    s_last_packet_ms = 0;
    s_auth_wait_deadline_ms = 0;
    s_client.stop();

    AdbItem item = {};
    while (xQueueReceive(s_queue, &item, 0) == pdTRUE) {
    }
}

bool connect_and_handshake() {
    if (!ensure_key()) {
        return false;
    }

    IPAddress address;
    if (!WiFi.hostByName(config::kFireTvHost, address)) {
        ESP_LOGW(TAG, "cannot resolve %s", config::kFireTvHost);
        return false;
    }

    ESP_LOGI(TAG, "connecting to %s:%u", config::kFireTvHost, kAdbPort);
    s_client.setTimeout(kConnectTimeoutMs);
    s_client.setNoDelay(true);
    if (!s_client.connect(address, kAdbPort, kConnectTimeoutMs)) {
        return false;
    }

    static constexpr char kHost[] = "host::";
    if (!send_packet(kCmdCnxn, kProtocolVersion, kMaxPayload,
                     reinterpret_cast<const uint8_t*>(kHost), sizeof(kHost))) {
        return false;
    }

    s_signature_sent = false;
    s_shell_ready = false;
    clear_sent_times();
    reset_shell_output();
    s_remote_id = 0;
    s_last_packet_ms = millis();
    return true;
}

bool handle_auth(const Packet& packet) {
    ESP_LOGD(TAG, "AUTH packet type=%u bytes=%u", static_cast<unsigned>(packet.arg0),
             static_cast<unsigned>(packet.length));
    if (packet.arg0 != kAuthToken || packet.length < 16) {
        return true;
    }
    if (!s_signature_sent) {
        std::vector<uint8_t> signature;
        if (!sign_token(packet.data, packet.length, signature)) {
            return false;
        }
        s_signature_sent = true;
        const bool sent = send_packet(kCmdAuth, kAuthSignature, 0, signature.data(),
                                      signature.size());
        ESP_LOGD(TAG, "signature sent=%d bytes=%u", sent ? 1 : 0,
                 static_cast<unsigned>(signature.size()));
        return sent;
    }

    std::string key;
    if (!public_key(key)) {
        return false;
    }
    key.push_back('\0');
    const bool sent = send_packet(kCmdAuth, kAuthPublicKey, 0,
                                  reinterpret_cast<const uint8_t*>(key.data()), key.size());
    ESP_LOGI(TAG,
             "public key sent=%d bytes=%u auth wait=%u ms auth backoff=%u ms", sent ? 1 : 0,
             static_cast<unsigned>(key.size()), static_cast<unsigned>(kAuthWaitMs),
             static_cast<unsigned>(kAuthRetryDelayMs));
    s_auth_wait_deadline_ms = millis() + kAuthWaitMs;
    s_reconnect_delay_ms = kAuthRetryDelayMs;
    return sent;
}

bool open_shell() {
    static constexpr char kService[] = "shell:";
    return send_packet(kCmdOpen, kLocalId, 0, reinterpret_cast<const uint8_t*>(kService),
                       sizeof(kService));
}

void send_queued_commands() {
    AdbItem item = {};
    size_t sent = 0;
    while (s_shell_ready && sent < kSendBatch && in_flight_count() < kMaxInFlight &&
           xQueueReceive(s_queue, &item, 0) == pdTRUE) {
        char command[256] = {};
        if (!build_shell_command(item.key, command, sizeof(command))) {
            ESP_LOGW(TAG, "invalid command '%s'", item.key);
            continue;
        }
        if (item.enqueued_us != 0) {
            s_stats.queue_wait.add(micros() - item.enqueued_us);
        }
        const size_t length = std::strlen(command);
        if (!send_packet(kCmdWrte, kLocalId, s_remote_id,
                         reinterpret_cast<const uint8_t*>(command), length)) {
            disconnect("write failed");
            return;
        }
        push_sent_time(micros());
        ++sent;
        ESP_LOGD(TAG, "shell: %s", command);
    }
}

void adb_task(void*) {
    uint32_t next_attempt_ms = 0;
    while (true) {
      try {
        if (s_client.connected()) {
            if (s_auth_wait_deadline_ms != 0 &&
                static_cast<int32_t>(millis() - s_auth_wait_deadline_ms) >= 0) {
                disconnect("auth wait expired");
                continue;
            }
            if (s_last_packet_ms != 0 &&
                static_cast<int32_t>(millis() - s_last_packet_ms) > 300000) {
                disconnect("auth timeout");
                continue;
            }
            send_queued_commands();
            if (!s_client.connected()) {
                disconnect("connection closed");
                continue;
            }
            if (s_client.available() < 24) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            Packet packet;
            if (!read_packet(packet)) {
                if (!s_client.connected()) {
                    disconnect("connection closed");
                }
                send_queued_commands();
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            s_last_packet_ms = millis();

            switch (packet.command) {
                case kCmdAuth:
                    if (!handle_auth(packet)) {
                        disconnect("authentication failed");
                    }
                    break;
                case kCmdCnxn:
                    s_auth_wait_deadline_ms = 0;
                    s_reconnect_delay_ms = kReconnectMs;
                    ESP_LOGI(TAG, "ADB connected");
                    if (!open_shell()) {
                        disconnect("shell open failed");
                    }
                    break;
                case kCmdOkay:
                    if (!s_shell_ready && packet.arg1 == kLocalId) {
                        s_remote_id = packet.arg0;
                        s_shell_ready = true;
                        s_connected.store(true);
                        clear_sent_times();
                        reset_shell_output();
                        ESP_LOGI(TAG, "shell ready");
                    } else if (s_shell_ready && packet.arg1 == kLocalId) {
                        const uint32_t sent_us = pop_sent_time();
                        if (sent_us != 0) {
                            s_stats.round_trip.add(micros() - sent_us);
                        }
                    }
                    break;
                case kCmdWrte:
                    send_packet(kCmdOkay, kLocalId, s_remote_id, nullptr, 0);
                    observe_shell_output(packet);
                    break;
                case kCmdClse:
                    disconnect("shell closed");
                    break;
                default:
                    ESP_LOGW(TAG, "unexpected packet 0x%08x", static_cast<unsigned>(packet.command));
                    break;
            }
            send_queued_commands();
            continue;
        }

        if (s_connected.load() || s_shell_ready) {
            disconnect("link lost");
        }
        if (static_cast<int32_t>(millis() - next_attempt_ms) >= 0) {
            next_attempt_ms = millis() + s_reconnect_delay_ms;
            if (!connect_and_handshake()) {
                s_client.stop();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
      } catch (...) {
        ESP_LOGE(TAG, "adb task exception; reconnecting");
        disconnect("exception");
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
}

}  // namespace

esp_err_t init() {
    if (s_queue != nullptr) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(kQueueLength, sizeof(AdbItem));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    return ensure_key() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t start() {
    if (s_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const BaseType_t result = xTaskCreate(adb_task, "adb", kTaskStack, nullptr, kTaskPriority, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool submit(const char* command) {
    if (s_queue == nullptr || command == nullptr || !s_connected.load()) {
        ++s_stats.dropped;
        return false;
    }
    AdbItem item = {};
    item.enqueued_us = micros();
    std::strncpy(item.key, command, sizeof(item.key) - 1);
    if (xQueueSend(s_queue, &item, pdMS_TO_TICKS(10)) != pdTRUE) {
        ++s_stats.dropped;
        return false;
    }
    return true;
}

const AdbStats& stats() {
    return s_stats;
}

bool connected() {
    return s_connected.load();
}

bool public_key_line(std::string& out) {
    if (!ensure_key()) {
        return false;
    }
    return public_key(out);
}

std::string public_key_fingerprint() {
    std::string key;
    if (!public_key_line(key)) {
        return {};
    }
    const size_t split = key.find(' ');
    const std::string encoded = key.substr(0, split);
    std::vector<uint8_t> decoded(encoded.size());
    size_t decoded_length = 0;
    if (mbedtls_base64_decode(decoded.data(), decoded.size(), &decoded_length,
                              reinterpret_cast<const unsigned char*>(encoded.data()),
                              encoded.size()) != 0) {
        return {};
    }
    unsigned char digest[16] = {};
    mbedtls_md5(decoded.data(), decoded_length, digest);
    char text[48] = {};
    for (size_t i = 0; i < sizeof(digest); ++i) {
        std::snprintf(text + i * 3, i + 1 == sizeof(digest) ? 3 : 4, "%02X%s", digest[i],
                      i + 1 == sizeof(digest) ? "" : ":");
    }
    return text;
}

}  // namespace irhub::adb
