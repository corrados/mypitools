#include "input/script_server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>

#include "config.hpp"
#include "control/control.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace irhub::script_server {
namespace {

constexpr char TAG[] = "script";
constexpr size_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 4;
constexpr size_t kMaxPending = 1024;

std::string normalize(std::string line) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto first = std::find_if(line.begin(), line.end(), not_space);
    const auto last = std::find_if(line.rbegin(), line.rend(), not_space).base();
    if (first >= last) {
        return {};
    }
    line = std::string(first, last);
    std::transform(line.begin(), line.end(), line.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return line;
}

void process_line(const std::string& line) {
    const std::string button = normalize(line);
    if (button.empty()) {
        return;
    }
    if (!control::post(button.c_str(), control::ButtonSource::ScriptSocket)) {
        ESP_LOGW(TAG, "control queue full, dropped %s", button.c_str());
    } else {
        ESP_LOGI(TAG, "received %s", button.c_str());
    }
}

void serve_client(int client) {
    std::string pending;
    char buffer[128];

    while (true) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            if (received < 0) {
                ESP_LOGW(TAG, "recv failed: %s", std::strerror(errno));
            }
            break;
        }
        pending.append(buffer, static_cast<size_t>(received));
        if (pending.size() > kMaxPending) {
            ESP_LOGW(TAG, "oversized command dropped");
            pending.clear();
        }

        size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
            process_line(pending.substr(0, newline));
            pending.erase(0, newline + 1);
        }
    }

    process_line(pending);
}

void script_task(void*) {
    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        ESP_LOGE(TAG, "socket failed: %s", std::strerror(errno));
        vTaskDelete(nullptr);
        return;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config::kScriptPort);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ESP_LOGE(TAG, "bind port %u failed: %s", static_cast<unsigned>(config::kScriptPort),
                 std::strerror(errno));
        close(server);
        vTaskDelete(nullptr);
        return;
    }

    if (listen(server, 1) != 0) {
        ESP_LOGE(TAG, "listen failed: %s", std::strerror(errno));
        close(server);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "listening on port %u", static_cast<unsigned>(config::kScriptPort));

    while (true) {
        sockaddr_in source = {};
        socklen_t source_length = sizeof(source);
        const int client = accept(server, reinterpret_cast<sockaddr*>(&source), &source_length);
        if (client < 0) {
            ESP_LOGW(TAG, "accept failed: %s", std::strerror(errno));
            continue;
        }

        serve_client(client);
        close(client);
    }
}

}  // namespace

esp_err_t start() {
    const BaseType_t result =
        xTaskCreate(script_task, "script", kTaskStack, nullptr, kTaskPriority, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace irhub::script_server
