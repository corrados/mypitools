#include "net/web_server.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "config.hpp"
#include "control/control.hpp"
#include "input/ps3_remote.hpp"
#include "net/adb.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace irhub::web_server {
namespace {

constexpr char TAG[] = "web";
constexpr size_t kTaskStack = 3072;
constexpr UBaseType_t kTaskPriority = 3;
constexpr size_t kRequestBuffer = 512;

constexpr char kIndexPage[] = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>IR Hub</title><style>body{font:16px system-ui;background:#111;color:#eee;margin:16px}button{font-size:15px;margin:3px;padding:10px 12px;border-radius:8px;border:0;background:#333;color:#fff}button:active{background:#08f}h2{font-size:15px;color:#aaa;margin:14px 0 4px}#status{margin:12px 0;color:#8f8}</style></head><body><h1>IR Hub</h1><div id="status">ready</div><div id="adb">adb: checking...</div><div id="buttons"></div><script>const groups=[["System",["EJECT","PLAYSTATION","SELECT","START","RETURN","BACK","OPTIONS","DISPLAY","TOPMENU","POPUP"]],["Numbers",["1","2","3","4","5","6","7","8","9","0"]],["Navigation",["UP","DOWN","LEFT","RIGHT","OK","AUIDO","ANGLE","SUBTITLE","CLEAR","TIME"]],["Colors",["RED","GREEN","BLUE","YELLOW"]],["Transport",["REWIND","PLAY","FORWARD","PREVIOUS","STOP","NEXT","SLOWREWIND","PAUSE","SLOWFORWARD"]],["Shoulder",["L1","L2","L3","R1","R2","R3","VIEW","X"]]];async function press(b){const r=await fetch('/press?button='+encodeURIComponent(b));document.getElementById('status').textContent=b+': '+await r.text();}async function refreshAdb(){const r=await fetch('/adb');document.getElementById('adb').textContent=await r.text();}setInterval(refreshAdb,2000);refreshAdb();const root=document.getElementById('buttons');for(const [name,keys] of groups){const h=document.createElement('h2');h.textContent=name;root.appendChild(h);const row=document.createElement('div');for(const key of keys){const b=document.createElement('button');b.textContent=key;b.onclick=()=>press(key);row.appendChild(b);}root.appendChild(row);}</script></body></html>)HTML";

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool url_decode(const char* input, char* output, size_t output_size) {
    size_t out = 0;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        unsigned char value = 0;
        if (input[i] == '%' && input[i + 1] != '\0' && input[i + 2] != '\0') {
            const int high = hex_value(input[i + 1]);
            const int low = hex_value(input[i + 2]);
            if (high < 0 || low < 0) {
                return false;
            }
            value = static_cast<unsigned char>((high << 4) | low);
            i += 2;
        } else if (input[i] == '+') {
            value = ' ';
        } else {
            value = static_cast<unsigned char>(input[i]);
        }
        if (out + 1 >= output_size) {
            return false;
        }
        output[out++] = static_cast<char>(value);
    }
    output[out] = '\0';
    return out > 0;
}

bool request_target(const char* request, char* target, size_t target_size) {
    if (std::strncmp(request, "GET ", 4) != 0) {
        return false;
    }
    const char* begin = request + 4;
    const char* end = std::strchr(begin, ' ');
    if (end == nullptr) {
        end = begin + std::strlen(begin);
    }
    const size_t length = static_cast<size_t>(end - begin);
    if (length == 0 || length >= target_size) {
        return false;
    }
    std::memcpy(target, begin, length);
    target[length] = '\0';
    return true;
}

void send_all(int client, const char* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const int written = send(client, data + offset, length - offset, 0);
        if (written <= 0) {
            return;
        }
        offset += static_cast<size_t>(written);
    }
}

void send_response(int client, const char* status, const char* content_type, const char* body,
                   size_t length) {
    char header[192];
    const int header_length =
        std::snprintf(header, sizeof(header),
                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                      status, content_type, static_cast<unsigned>(length));
    if (header_length <= 0 || static_cast<size_t>(header_length) >= sizeof(header)) {
        return;
    }
    send_all(client, header, static_cast<size_t>(header_length));
    if (length > 0) {
        send_all(client, body, length);
    }
}

void send_text(int client, const char* text) {
    send_response(client, "200 OK", "text/plain; charset=utf-8", text, std::strlen(text));
}

void handle_request(int client, const char* request) {
    char target[128] = {};
    if (!request_target(request, target, sizeof(target))) {
        send_response(client, "400 Bad Request", "text/plain; charset=utf-8", "bad request", 11);
        return;
    }

    char path[64] = {};
    const char* query = std::strchr(target, '?');
    const size_t path_length =
        query == nullptr ? std::strlen(target) : static_cast<size_t>(query - target);
    if (path_length == 0 || path_length >= sizeof(path)) {
        send_response(client, "400 Bad Request", "text/plain; charset=utf-8", "bad request", 11);
        return;
    }
    std::memcpy(path, target, path_length);
    path[path_length] = '\0';

    if (std::strcmp(path, "/") == 0) {
        send_response(client, "200 OK", "text/html; charset=utf-8", kIndexPage,
                      sizeof(kIndexPage) - 1);
        return;
    }
    if (std::strcmp(path, "/favicon.ico") == 0) {
        send_response(client, "204 No Content", "text/plain", "", 0);
        return;
    }
    if (std::strcmp(path, "/adb") == 0) {
        send_text(client, adb::connected() ? "adb=connected" : "adb=disconnected");
        return;
    }
    if (std::strcmp(path, "/state") == 0) {
        char body[64] = {};
        std::snprintf(body, sizeof(body), "state=%s", control::state_name(control::current_state()));
        send_text(client, body);
        return;
    }
    if (std::strcmp(path, "/heap") == 0) {
        char body[64] = {};
        std::snprintf(body, sizeof(body), "heap=%u min=%u",
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMinFreeHeap()));
        send_text(client, body);
        return;
    }
    if (std::strcmp(path, "/press") == 0 && query != nullptr) {
        const char* encoded = std::strstr(query, "button=");
        char button[32] = {};
        if (encoded == nullptr || !url_decode(encoded + 7, button, sizeof(button))) {
            send_text(client, "error");
            return;
        }
        std::transform(button, button + std::strlen(button), button,
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const char* command = input::ps3_convert(button);
        if (!control::post(command, control::ButtonSource::Internal)) {
            ESP_LOGW(TAG, "control queue full, dropped %s -> %s", button, command);
            send_text(client, "error");
            return;
        }
        ESP_LOGI(TAG, "received %s -> %s", button, command);
        send_text(client, "ok");
        return;
    }
    send_response(client, "404 Not Found", "text/plain; charset=utf-8", "not found", 9);
}

void serve_client(int client) {
    struct timeval timeout = {};
    timeout.tv_sec = 3;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char request[kRequestBuffer] = {};
    size_t used = 0;
    while (used + 1 < sizeof(request)) {
        const int received = recv(client, request + used, sizeof(request) - used - 1, 0);
        if (received <= 0) {
            break;
        }
        used += static_cast<size_t>(received);
        request[used] = '\0';
        if (std::strstr(request, "\r\n\r\n") != nullptr) {
            break;
        }
    }
    if (used > 0) {
        handle_request(client, request);
    }
}

void web_task(void*) {
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
    address.sin_port = htons(config::kWebPort);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ESP_LOGE(TAG, "bind port %u failed: %s", static_cast<unsigned>(config::kWebPort),
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

    ESP_LOGI(TAG, "listening on port %u", static_cast<unsigned>(config::kWebPort));
    while (true) {
        const int client = accept(server, nullptr, nullptr);
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
    const BaseType_t result = xTaskCreate(web_task, "web", kTaskStack, nullptr, kTaskPriority, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace irhub::web_server
