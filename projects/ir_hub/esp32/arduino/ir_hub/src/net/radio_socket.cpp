#include "net/radio_socket.hpp"

#include <Arduino.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>

#include "config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace irhub::radio_socket {
namespace {

constexpr char TAG[] = "radio";
constexpr size_t kQueueLength = 8;
constexpr size_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 3;
constexpr int kTimeoutMs = 5000;

QueueHandle_t s_queue = nullptr;

bool wait_socket(int fd, bool for_write, int timeout_ms) {
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(fd, &descriptors);
    struct timeval timeout = {};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    if (for_write) {
        return select(fd + 1, nullptr, &descriptors, nullptr, &timeout) > 0;
    }
    return select(fd + 1, &descriptors, nullptr, nullptr, &timeout) > 0;
}

bool send_all(int fd, const char* data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        const int written = send(fd, data + sent, length - sent, 0);
        if (written > 0) {
            sent += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            wait_socket(fd, true, kTimeoutMs)) {
            continue;
        }
        return false;
    }
    return true;
}

bool request_power(bool on) {
    char path[64] = {};
    std::snprintf(path, sizeof(path), "/cm?cmnd=Power%%20%s", on ? "On" : "Off");

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(80);
    if (inet_pton(AF_INET, config::kRadioSocketIp, &address.sin_addr) != 1) {
        hostent* host = gethostbyname(config::kRadioSocketHost);
        if (host == nullptr || host->h_addr_list == nullptr || host->h_addr_list[0] == nullptr) {
            ESP_LOGD(TAG, "address lookup failed for %s", config::kRadioSocketHost);
            return false;
        }
        std::memcpy(&address.sin_addr, host->h_addr_list[0], sizeof(address.sin_addr));
    }

    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ESP_LOGD(TAG, "socket failed errno=%d", errno);
        return false;
    }
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    const int result = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result != 0 && (result >= 0 || errno != EINPROGRESS)) {
        ESP_LOGD(TAG, "connect failed errno=%d", errno);
        close(fd);
        return false;
    }
    if (!wait_socket(fd, true, kTimeoutMs)) {
        ESP_LOGD(TAG, "connect timeout");
        close(fd);
        return false;
    }
    int socket_error = 0;
    socklen_t error_length = sizeof(socket_error);
    const int option_result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length);
    if (option_result != 0 || socket_error != 0) {
        ESP_LOGD(TAG, "socket check failed getsockopt=%d so_error=%d errno=%d", option_result,
                 socket_error, errno);
        close(fd);
        return false;
    }
    char request[192] = {};
    const int request_length =
        std::snprintf(request, sizeof(request),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path,
                      config::kRadioSocketHost);
    if (request_length <= 0 || !send_all(fd, request, static_cast<size_t>(request_length))) {
        ESP_LOGD(TAG, "send failed errno=%d", errno);
        close(fd);
        return false;
    }
    wait_socket(fd, false, kTimeoutMs);
    char discard[64] = {};
    while (recv(fd, discard, sizeof(discard), 0) > 0) {
    }
    close(fd);
    return true;
}

void radio_task(void*) {
    bool on = false;
    while (true) {
        if (xQueueReceive(s_queue, &on, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "Power %s request", on ? "On" : "Off");
        request_power(on);
        ESP_LOGI(TAG, "Power %s request complete", on ? "On" : "Off");
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
        xTaskCreatePinnedToCore(radio_task, "radio", kTaskStack, nullptr, kTaskPriority, nullptr, config::kAppCore);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool switch_power(bool on) {
    if (s_queue == nullptr) {
        return false;
    }
    return xQueueSend(s_queue, &on, pdMS_TO_TICKS(10)) == pdTRUE;
}

}  // namespace irhub::radio_socket
