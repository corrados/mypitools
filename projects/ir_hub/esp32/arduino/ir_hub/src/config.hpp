#pragma once

#include <cstdint>

#ifndef IR_HUB_IR_GPIO
#define IR_HUB_IR_GPIO 22
#endif

#ifndef IR_HUB_RGB_SCLK_GPIO
#define IR_HUB_RGB_SCLK_GPIO 18
#endif

#ifndef IR_HUB_RGB_MOSI_GPIO
#define IR_HUB_RGB_MOSI_GPIO 23
#endif

#ifndef IR_HUB_RGB_VALUE
#define IR_HUB_RGB_VALUE 10
#endif

#ifndef IR_HUB_SCRIPT_PORT
#define IR_HUB_SCRIPT_PORT 3333
#endif

#ifndef IR_HUB_STANDBY_SECONDS
#define IR_HUB_STANDBY_SECONDS 1800
#endif

#ifndef IR_HUB_WIFI_SSID
#define IR_HUB_WIFI_SSID ""
#endif

#ifndef IR_HUB_WIFI_PASSWORD
#define IR_HUB_WIFI_PASSWORD ""
#endif

#ifndef IR_HUB_WIFI_MAX_RETRY
#define IR_HUB_WIFI_MAX_RETRY 10
#endif

#ifndef IR_HUB_PS3_REMOTE_MAC
#define IR_HUB_PS3_REMOTE_MAC "00:1E:3D:10:CB:F0"
#endif

#ifndef IR_HUB_FIRETV_HOST
#define IR_HUB_FIRETV_HOST "firetv1"
#endif

#ifndef IR_HUB_RADIO_SOCKET_HOST
#define IR_HUB_RADIO_SOCKET_HOST "radiosocket"
#endif

namespace irhub::config {

inline constexpr int kIrPin = IR_HUB_IR_GPIO;
inline constexpr int kRgbSclkPin = IR_HUB_RGB_SCLK_GPIO;
inline constexpr int kRgbMosiPin = IR_HUB_RGB_MOSI_GPIO;
inline constexpr uint8_t kRgbValue = IR_HUB_RGB_VALUE;
inline constexpr uint16_t kScriptPort = IR_HUB_SCRIPT_PORT;
inline constexpr uint32_t kStandbySeconds = IR_HUB_STANDBY_SECONDS;
inline constexpr uint32_t kStandbyMs = kStandbySeconds * 1000U;

inline constexpr char kWifiSsid[] = IR_HUB_WIFI_SSID;
inline constexpr char kWifiPassword[] = IR_HUB_WIFI_PASSWORD;
inline constexpr int kWifiMaxRetry = IR_HUB_WIFI_MAX_RETRY;

inline constexpr char kPs3RemoteMac[] = IR_HUB_PS3_REMOTE_MAC;
inline constexpr char kFireTvHost[] = IR_HUB_FIRETV_HOST;
inline constexpr char kRadioSocketHost[] = IR_HUB_RADIO_SOCKET_HOST;

}  // namespace irhub::config
