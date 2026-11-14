#include "output/rgb_led.hpp"

#include <Arduino.h>

#include "config.hpp"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace irhub::rgb_led {
namespace {

constexpr char TAG[] = "rgb_led";
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr int kClockHz = 1000000;
constexpr uint32_t kLatchDelayUs = 600;

spi_device_handle_t s_device = nullptr;
SemaphoreHandle_t s_mutex = nullptr;

}  // namespace

esp_err_t init() {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus = {};
    bus.mosi_io_num = config::kRgbMosiPin;
    bus.miso_io_num = -1;
    bus.sclk_io_num = config::kRgbSclkPin;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 3;

    esp_err_t err = spi_bus_initialize(kSpiHost, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t device = {};
    device.clock_speed_hz = kClockHz;
    device.mode = 0;
    device.spics_io_num = -1;
    device.queue_size = 1;

    err = spi_bus_add_device(kSpiHost, &device, &s_device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    set(0, 0, 0);
    return ESP_OK;
}

void set(uint8_t r, uint8_t g, uint8_t b) {
    if (s_device == nullptr || s_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "SPI mutex timeout");
        return;
    }

    spi_transaction_t transaction = {};
    transaction.length = 24;
    transaction.flags = SPI_TRANS_USE_TXDATA;
    transaction.tx_data[0] = r;
    transaction.tx_data[1] = g;
    transaction.tx_data[2] = b;

    const esp_err_t err = spi_device_polling_transmit(s_device, &transaction);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
    }
    esp_rom_delay_us(kLatchDelayUs);
    xSemaphoreGive(s_mutex);
}

}  // namespace irhub::rgb_led
