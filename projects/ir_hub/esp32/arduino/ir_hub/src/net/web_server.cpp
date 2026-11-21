#include "net/web_server.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <string>

#include "config.hpp"
#include "control/control.hpp"
#include "input/ps3_remote.hpp"
#include "net/adb.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace irhub::web_server {
namespace {

constexpr char TAG[] = "web";
constexpr size_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 3;
constexpr size_t kRequestBuffer = 384;
constexpr size_t kResponseBuffer = 2048;
constexpr uint32_t kClientTimeoutMs = 10000;

constexpr char kIndexPage[] = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>IR Hub</title><style>body{font:16px system-ui;background:#111;color:#eee;margin:16px}button{font-size:15px;margin:3px;padding:10px 12px;border-radius:8px;border:0;background:#333;color:#fff}button:active{background:#08f}h2{font-size:15px;color:#aaa;margin:14px 0 4px}#status{margin:12px 0;color:#8f8}</style></head><body><h1>IR Hub</h1><div id="status">ready</div><div id="buttons"></div><script>const groups=[["System",["EJECT","PLAYSTATION","SELECT","START","RETURN","BACK","OPTIONS","DISPLAY","TOPMENU","POPUP"]],["Numbers",["1","2","3","4","5","6","7","8","9","0"]],["Navigation",["UP","DOWN","LEFT","RIGHT","OK","AUIDO","ANGLE","SUBTITLE","CLEAR","TIME"]],["Colors",["RED","GREEN","BLUE","YELLOW"]],["Transport",["REWIND","PLAY","FORWARD","PREVIOUS","STOP","NEXT","SLOWREWIND","PAUSE","SLOWFORWARD"]],["Shoulder",["L1","L2","L3","R1","R2","R3","VIEW","X"]]];function press(b){fetch('/press?button='+encodeURIComponent(b));document.getElementById('status').textContent=b+': sent';}const root=document.getElementById('buttons');for(const [name,keys] of groups){const h=document.createElement('h2');h.textContent=name;root.appendChild(h);const row=document.createElement('div');for(const key of keys){const b=document.createElement('button');b.textContent=key;b.onclick=()=>press(key);row.appendChild(b);}root.appendChild(row);}</script></body></html>)HTML";

struct Client {
    int fd = -1;
    char* request = nullptr;
    size_t request_length = 0;
    char* response = nullptr;
    size_t response_length = 0;
    size_t response_offset = 0;
    uint32_t response_us = 0;
    uint32_t last_active = 0;
    bool keep_alive = false;
};

bool contains_ci(const char* haystack, const char* needle) {
    const size_t needle_length = std::strlen(needle);
    for (const char* p = haystack; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < needle_length && p[i] != '\0' &&
               std::tolower(static_cast<unsigned char>(p[i])) ==
                   std::tolower(static_cast<unsigned char>(needle[i]))) {
            ++i;
        }
        if (i == needle_length) {
            return true;
        }
    }
    return false;
}

bool request_keep_alive(const char* request) {
    if (std::strstr(request, "HTTP/1.1") == nullptr) {
        return false;
    }
    return !contains_ci(request, "connection: close");
}

Client s_client;
WebStats s_web_stats;
char s_request_buffer[kRequestBuffer] = {};
char s_response_buffer[kResponseBuffer] = {};

bool would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
}

void set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

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

bool prepare_response(Client& client, const char* status, const char* content_type,
                      const char* body, size_t length) {
    const int header_length =
        std::snprintf(client.response, kResponseBuffer,
                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                      "Cache-Control: no-store\r\nConnection: %s\r\n\r\n",
                      status, content_type, static_cast<unsigned>(length),
                      client.keep_alive ? "keep-alive" : "close");
    if (header_length <= 0 || static_cast<size_t>(header_length) >= kResponseBuffer) {
        return false;
    }
    const size_t header = static_cast<size_t>(header_length);
    if (length > kResponseBuffer - header) {
        return false;
    }
    if (length > 0) {
        std::memcpy(client.response + header, body, length);
    }
    client.response_length = header + length;
    client.response_offset = 0;
    return true;
}

bool prepare_text(Client& client, const char* text) {
    return prepare_response(client, "200 OK", "text/plain; charset=utf-8", text, std::strlen(text));
}

void build_response_impl(Client& client) {
    char target[128] = {};
    if (!request_target(client.request, target, sizeof(target))) {
        prepare_response(client, "400 Bad Request", "text/plain; charset=utf-8", "bad request", 11);
        return;
    }

    char path[64] = {};
    const char* query = std::strchr(target, '?');
    const size_t path_length =
        query == nullptr ? std::strlen(target) : static_cast<size_t>(query - target);
    if (path_length == 0 || path_length >= sizeof(path)) {
        prepare_response(client, "400 Bad Request", "text/plain; charset=utf-8", "bad request", 11);
        return;
    }
    std::memcpy(path, target, path_length);
    path[path_length] = '\0';

    if (std::strcmp(path, "/") == 0) {
        prepare_response(client, "200 OK", "text/html; charset=utf-8", kIndexPage,
                         sizeof(kIndexPage) - 1);
        return;
    }
    if (std::strcmp(path, "/favicon.ico") == 0) {
        prepare_response(client, "204 No Content", "text/plain", "", 0);
        return;
    }
    if (std::strcmp(path, "/adb") == 0) {
        prepare_text(client, adb::connected() ? "adb=connected" : "adb=disconnected");
        return;
    }
    if (std::strcmp(path, "/state") == 0) {
        char body[64] = {};
        std::snprintf(body, sizeof(body), "state=%s", control::state_name(control::current_state()));
        prepare_text(client, body);
        return;
    }
    if (std::strcmp(path, "/heap") == 0) {
        char body[96] = {};
        std::snprintf(body, sizeof(body), "heap=%u min=%u rst=%u",
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMinFreeHeap()),
                      static_cast<unsigned>(esp_reset_reason()));
        prepare_text(client, body);
        return;
    }
    if (std::strcmp(path, "/latency") == 0) {
        static char body[768];
        format_stats(body, sizeof(body));
        prepare_text(client, body);
        return;
    }
    if (std::strcmp(path, "/press") == 0 && query != nullptr) {
        const char* encoded = std::strstr(query, "button=");
        char button[32] = {};
        if (encoded == nullptr || !url_decode(encoded + 7, button, sizeof(button))) {
            prepare_text(client, "error");
            return;
        }
        std::transform(button, button + std::strlen(button), button,
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const char* command = input::ps3_convert(button);
        if (!control::post(command, control::ButtonSource::Internal)) {
            ++s_web_stats.dropped;
            ESP_LOGW(TAG, "control queue full, dropped %s -> %s", button, command);
            prepare_text(client, "error");
            return;
        }
        ESP_LOGD(TAG, "received %s -> %s", button, command);
        prepare_response(client, "204 No Content", "text/plain", "", 0);
        return;
    }
    prepare_response(client, "404 Not Found", "text/plain; charset=utf-8", "not found", 9);
}

void build_response(Client& client) {
    const uint32_t started = micros();
    build_response_impl(client);
    if (client.response_length > 0) {
        s_web_stats.handle.add(micros() - started);
        client.response_us = micros();
    }
}

void close_client(Client& client) {
    if (client.fd >= 0) {
        close(client.fd);
    }
    client.fd = -1;
    client.request_length = 0;
    client.response_length = 0;
    client.response_offset = 0;
    client.response_us = 0;
    client.keep_alive = false;
}

void accept_clients(int server) {
    while (s_client.fd < 0) {
        const int fd = accept(server, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        set_nonblocking(fd);
        const int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        const int send_buffer = 16384;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        s_request_buffer[0] = '\0';
        s_response_buffer[0] = '\0';
        s_client.fd = fd;
        s_client.request = s_request_buffer;
        s_client.response = s_response_buffer;
        s_client.request_length = 0;
        s_client.response_length = 0;
        s_client.response_offset = 0;
        s_client.response_us = 0;
        s_client.last_active = millis();
    }
}

void service_client(Client& client) {
    if (client.fd < 0) {
        return;
    }
    if (static_cast<int32_t>(millis() - client.last_active) < 0 ||
        millis() - client.last_active > kClientTimeoutMs) {
        close_client(client);
        return;
    }

    if (client.response_length > client.response_offset) {
        const size_t remaining = client.response_length - client.response_offset;
        const size_t chunk = remaining < 256 ? remaining : 256;
        const int written = send(client.fd, client.response + client.response_offset, chunk,
                                 MSG_DONTWAIT);
        if (written > 0) {
            client.response_offset += static_cast<size_t>(written);
            client.last_active = millis();
            if (client.response_offset >= client.response_length) {
                s_web_stats.tx.add(micros() - client.response_us);
                client.response_us = 0;
                if (client.keep_alive) {
                    client.request_length = 0;
                    client.response_length = 0;
                    client.response_offset = 0;
                    client.response_us = 0;
                    client.request[0] = '\0';
                } else {
                    close_client(client);
                }
            }
        } else if (written < 0 && !would_block()) {
            close_client(client);
        }
        return;
    }

    while (client.request_length + 1 < kRequestBuffer) {
        const int received = recv(client.fd, client.request + client.request_length,
                                  kRequestBuffer - client.request_length - 1, MSG_DONTWAIT);
        if (received > 0) {
            client.request_length += static_cast<size_t>(received);
            client.request[client.request_length] = '\0';
            client.last_active = millis();
            if (std::strstr(client.request, "\r\n\r\n") != nullptr) {
                client.keep_alive = request_keep_alive(client.request);
                build_response(client);
                return;
            }
            continue;
        }
        if (received == 0) {
            close_client(client);
        } else if (!would_block()) {
            close_client(client);
        }
        return;
    }
    close_client(client);
}

void web_task(void*) {
    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        ESP_LOGE(TAG, "socket failed: %s", std::strerror(errno));
        vTaskDelete(nullptr);
        return;
    }

    set_nonblocking(server);
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
    if (listen(server, 32) != 0) {
        ESP_LOGE(TAG, "listen failed: %s", std::strerror(errno));
        close(server);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "listening on port %u", static_cast<unsigned>(config::kWebPort));
    while (true) {
        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_SET(server, &read_set);
        int max_fd = server;
        if (s_client.fd >= 0) {
            FD_SET(s_client.fd, &read_set);
            if (s_client.response_length > s_client.response_offset) {
                FD_SET(s_client.fd, &write_set);
            }
            if (s_client.fd > max_fd) {
                max_fd = s_client.fd;
            }
        }

        struct timeval timeout = {};
        timeout.tv_usec = 50000;
        select(max_fd + 1, &read_set, &write_set, nullptr, &timeout);
        if (FD_ISSET(server, &read_set)) {
            accept_clients(server);
        }
        service_client(s_client);
    }
}

}  // namespace

size_t format_stats(char* out, size_t size) {
    const control::ControlStats& control_stats = control::stats();
    const adb::AdbStats& adb_stats = adb::stats();
    size_t offset = 0;
    char line[160] = {};
    diag::format("web.handle", s_web_stats.handle, line, sizeof(line));
    offset += static_cast<size_t>(std::snprintf(out + offset, size - offset, "%s\n", line));
    diag::format("web.tx", s_web_stats.tx, line, sizeof(line));
    offset += static_cast<size_t>(std::snprintf(out + offset, size - offset, "%s\n", line));
    diag::format("control.wait", control_stats.queue_wait, line, sizeof(line));
    offset += static_cast<size_t>(std::snprintf(out + offset, size - offset, "%s\n", line));
    diag::format("control.exec", control_stats.handler, line, sizeof(line));
    offset += static_cast<size_t>(std::snprintf(out + offset, size - offset, "%s\n", line));
    diag::format("adb.wait", adb_stats.queue_wait, line, sizeof(line));
    offset += static_cast<size_t>(std::snprintf(out + offset, size - offset, "%s\n", line));
    diag::format("adb.rtt", adb_stats.round_trip, line, sizeof(line));
    offset += static_cast<size_t>(std::snprintf(out + offset, size - offset, "%s\n", line));
    offset += static_cast<size_t>(
        std::snprintf(out + offset, size - offset, "drops web=%u control=%u adb=%u adb_timeouts=%u\n",
                      static_cast<unsigned>(s_web_stats.dropped),
                      static_cast<unsigned>(control_stats.dropped),
                      static_cast<unsigned>(adb_stats.dropped),
                      static_cast<unsigned>(adb_stats.timeouts)));
    return offset;
}

esp_err_t start() {
    const BaseType_t result = xTaskCreatePinnedToCore(web_task, "web", kTaskStack, nullptr, kTaskPriority, nullptr, config::kAppCore);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace irhub::web_server
