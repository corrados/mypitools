#pragma once

#include <cstdint>

#include "sdkconfig.h"

namespace irhub::config {

inline constexpr int kIrPin = CONFIG_IR_HUB_IR_GPIO;
inline constexpr int kRgbSclkPin = CONFIG_IR_HUB_RGB_SCLK_GPIO;
inline constexpr int kRgbMosiPin = CONFIG_IR_HUB_RGB_MOSI_GPIO;
inline constexpr uint8_t kRgbValue = CONFIG_IR_HUB_RGB_VALUE;
inline constexpr uint16_t kScriptPort = CONFIG_IR_HUB_SCRIPT_PORT;
inline constexpr uint32_t kStandbySeconds = CONFIG_IR_HUB_STANDBY_SECONDS;
inline constexpr uint32_t kStandbyMs = kStandbySeconds * 1000U;

inline constexpr char kWifiSsid[] = CONFIG_IR_HUB_WIFI_SSID;
inline constexpr char kWifiPassword[] = CONFIG_IR_HUB_WIFI_PASSWORD;
inline constexpr int kWifiMaxRetry = CONFIG_IR_HUB_WIFI_MAX_RETRY;

inline constexpr char kPs3RemoteMac[] = CONFIG_IR_HUB_PS3_REMOTE_MAC;
inline constexpr char kFireTvHost[] = CONFIG_IR_HUB_FIRETV_HOST;
inline constexpr char kRadioSocketHost[] = CONFIG_IR_HUB_RADIO_SOCKET_HOST;

}  // namespace irhub::config
