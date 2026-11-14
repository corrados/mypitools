#include <Arduino.h>
#include <Esp.h>

void setup() {
    Serial.begin(115200);
    delay(750);

    Serial.println();
    Serial.println("IR hub Arduino smoke test");
    Serial.printf("chip=%s revision=%d cores=%d\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores());
    Serial.printf("cpu=%u MHz flash=%u MHz heap=%u\n", ESP.getCpuFreqMHz(),
                  ESP.getFlashChipSpeed() / 1000000U, ESP.getFreeHeap());
    Serial.printf("mac=%04X%08X\n", static_cast<uint16_t>(ESP.getEfuseMac() >> 32),
                  static_cast<uint32_t>(ESP.getEfuseMac()));
}

void loop() {
    Serial.printf("alive uptime=%lu heap=%u\n", millis(), ESP.getFreeHeap());
    delay(1000);
}
