Port ../../ir_hub.py to ESP32
=============================

Current status
--------------

`ir_hub.py` currently runs stable on a Raspberry Pi 3B+. An RGB LED and four IR transmitter diodes are connected.

Note that I do not use a standard PS3 controller but the PS3 BD remote, which is like a normal TV remote but with Bluetooth instead of IR.

Goal
----

Port the complete behavior of `ir_hub.py` to C++ on the original ESP32, including:

- PS3 BD remote input over Bluetooth Classic.
- Precise 36/38 kHz IR transmission for the soundbar, projector, LED strip, TV, and DVD player.
- Fire TV control over ADB.
- Radio socket control over HTTP.
- The six-state control model and the RGB status LED.
- The existing user-facing button behavior, including long-press/repeat behavior and timed startup sequences.

The ESP32 is the original 2x Xtensa LX6 version (240 MHz dual core). It has Bluetooth Classic, Wi-Fi, RMT, SPI, and enough RAM for this application. Do **not** target ESP32-S2/S3/C3/C6: they do not provide Bluetooth Classic.

Recommended decisions
---------------------

1. **Framework:** ESP-IDF v5.5.x with C++17. Do not use Arduino as the primary framework.
   - ESP-IDF provides the supported Bluetooth Classic HID Host API, RMT carrier modulation, Wi-Fi/lwIP, mbedTLS, event loops, and coexistence control.
   - Arduino hides too much of the Bluetooth Classic and RMT configuration for this project.
2. **Bluetooth:** Start with the ESP-IDF `esp_hid_host` component. Keep a raw L2CAP HID-host fallback because the PS3 BD remote is old and may expose report/pairing quirks that the generic host does not handle.
3. **IR:** Generate mark/space envelopes in software and let the RMT peripheral generate the carrier. Do not bit-bang the carrier and do not use `IRremoteESP8266` as the runtime encoder.
   - RMT handles 36/38 kHz carrier timing in hardware.
   - Software only needs to emit durations in microseconds.
4. **RGB LED:** Treat it as a WS2801-compatible SPI LED until the hardware is verified. The Python code writes exactly three bytes over SPI mode 0 at 1 MHz, which matches WS2801-style wiring. WS2812 is only a fallback and requires a different driver.
5. **ADB:** Implement a small ADB-over-TCP client instead of spawning an `adb` binary. Prefer a device-generated RSA key stored persistently; reusing the Pi's `~/.android/adbkey` is acceptable only for temporary bring-up.
6. **Input socket:** Replace the Raspberry Pi Unix socket with a small TCP line-oriented command server, or optionally support both TCP and UART. A Unix socket is not a natural ESP-IDF interface.
7. **Serialization:** Run button handling in one control task. Run IR transmissions through one queue-backed IR worker so waveforms never overlap. Bluetooth, ADB, HTTP, and LED work remain separate tasks.

Target hardware and pin plan
----------------------------

| Function | ESP32 pin | Notes |
|---|---:|---|
| IR transmitter | GPIO 22 | Same logical output as the Python `out_pin`. Four IR diodes may be driven through the existing transistor/driver stage. |
| RGB LED SPI SCLK | GPIO 18 | SPI clock, mode 0, 1 MHz. |
| RGB LED SPI MOSI | GPIO 23 | Three bytes per update for one WS2801 LED. |
| Optional WS2812 data | GPIO 16 | Only if the LED is later identified as WS2812. |
| Optional IR receiver | GPIO 27 | Not required by the current Python program. |
| Power/ground | — | Use a proper 5 V/3.3 V supply; do not power IR diodes directly from a GPIO. |

Keep the IR output current-limited and verify the existing driver polarity. The ESP32 GPIO is a logic signal, not the IR diode power source.

System architecture
-------------------

```text
PS3 BD remote --Bluetooth Classic--> ps3_remote task --+
                                                       |
TCP/UART command socket -------------------------------+--> control queue
                                                       |
                                                       +--> state_machine task
                                                              |
                          +-----------------------------------+------------------+
                          |                                   |                  |
                     IR submit queue                     ADB queue          LED/status
                          |                                   |                  |
                     RMT IR worker                       ADB client        SPI LED task
                          |                                   |
                     IR diodes                         Fire TV :5555

radio socket <---- esp_http_client ---- radio timer / control task
```

There must be exactly one owner of each hardware resource:

- RMT TX channel: IR worker only.
- SPI bus: RGB LED task only, protected by a mutex if a second SPI user is added.
- Bluetooth HID connection: Bluetooth task only.
- ADB socket: ADB task only.
- State variables: control task only.

Project layout
--------------

Create the ESP-IDF project under the existing project directory:

```text
projects/ir_hub/
  IMPLEMENTATION_PLAN.md
  ir_hub.py                         # reference implementation, not compiled
  esp32/
    CMakeLists.txt
    sdkconfig.defaults
    partitions.csv
    main/
      CMakeLists.txt
      app_main.cpp
      config.hpp
      control/
        state_machine.hpp
        state_machine.cpp
        button_router.hpp
        button_router.cpp
      input/
        ps3_remote.hpp
        ps3_remote.cpp
        script_server.hpp
        script_server.cpp
      ir/
        ir_tx.hpp
        ir_tx.cpp
        ir_codes.hpp
        ir_codes.cpp
      output/
        rgb_led.hpp
        rgb_led.cpp
      net/
        adb_client.hpp
        adb_client.cpp
        radio_socket.hpp
        radio_socket.cpp
      util/
        time.hpp
        logging.hpp
    test/
      host/
        test_ir_encoding.cpp
        test_state_machine.cpp
        test_key_maps.cpp
```

The `test/host` tests should compile on a normal Linux host where possible. Keep pure logic (key maps, report decoding, waveform construction, state transitions) independent of ESP-IDF headers.

Runtime model
-------------

Use these FreeRTOS tasks and fixed-size queues. Avoid unbounded dynamic allocation after startup.

| Task | Responsibility | Priority suggestion |
|---|---|---:|
| `control_task` | Consume button events, own all state variables, run transition sequences. | 8 |
| `ir_task` | Consume IR requests, build RMT symbols, transmit, serialize all IR. | 10 |
| `ps3_remote_task` | HID host callback/raw L2CAP receive, report decoding, button repeat timing. | 9 |
| `script_server_task` | Accept TCP/UART commands and enqueue button names. | 4 |
| `adb_task` | Connect/authenticate, open shell, execute Fire TV commands, reconnect every 5 s. | 5 |
| `rgb_task` | Consume RGB updates and write the SPI LED, or perform short mutex-protected writes. | 3 |
| `radio_timer` | `esp_timer` callback that requests radio socket off after 1800 s. | timer |

Use one `control_event_t` queue for all button presses:

```cpp
struct ControlEvent {
    enum class Source : uint8_t { Bluetooth, ScriptSocket, Internal };
    Source source;
    char button[16];       // uppercase name, e.g. "VOL+", "POWER", "FIRETVBEAM 103"
};
```

All button names must be normalized to the exact strings used by `ir_hub.py`. Preserve the typo `AUIDO` in the PS3 map and in `playstation_convert`; changing it silently breaks the mapping.

Configuration constants to keep identical initially:

```cpp
constexpr gpio_num_t kIrPin        = GPIO_NUM_22;
constexpr gpio_num_t kLedSclkPin   = GPIO_NUM_18;
constexpr gpio_num_t kLedMosiPin   = GPIO_NUM_23;
constexpr uint8_t    kRgbValue     = 10;       // 0..255
constexpr uint32_t   kStandbyMs    = 1800 * 1000;
constexpr char       kPs3RemoteMac[] = "00:1E:3D:10:CB:F0";
```

Do not hard-code Wi-Fi credentials, the Fire TV IP address, or the ADB private key. Put them in `menuconfig`, a provisioning file, or NVS. The host name `firetv1` must be resolvable or replaced by a configured IPv4 address.

IR subsystem
------------

### Why RMT

The Raspberry Pi implementation creates an explicit list of carrier on/off pulses with `pigpio`:

- 38 kHz default, 36 kHz for the soundbar.
- 50% duty cycle.
- A mark is a burst of carrier cycles; a space is a low-level gap.
- RC6 additionally uses a 444 us base unit `T`.

On the ESP32, do **not** generate every 13 us carrier half-cycle in software. Configure the RMT TX channel with a 36/38 kHz carrier and emit only marks and spaces. The RMT hardware produces the carrier while the channel output is high.

### RMT configuration

Use the new ESP-IDF RMT TX driver:

```cpp
rmt_tx_channel_config_t tx_cfg = {};
tx_cfg.gpio_num = kIrPin;
tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
tx_cfg.resolution_hz = 1'000'000;   // 1 us per tick
tx_cfg.mem_block_symbols = 64;
tx_cfg.trans_queue_depth = 8;
tx_cfg.flags.invert_out = false;
tx_cfg.flags.with_dma = false;

rmt_channel_handle_t tx = nullptr;
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &tx));

rmt_carrier_config_t carrier = {};
carrier.frequency_hz = 38000;       // or 36000 for BAR
carrier.duty_cycle = 0.5f;
carrier.flags.polarity_active_low = false;
ESP_ERROR_CHECK(rmt_apply_carrier(tx, &carrier));

rmt_copy_encoder_config_t enc_cfg = {};
rmt_encoder_handle_t encoder = nullptr;
ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &encoder));
ESP_ERROR_CHECK(rmt_enable(tx));
```

Verify the carrier polarity with a scope or an IR receiver before debugging key codes. If the output is inverted, change `polarity_active_low` or `invert_out` once, at the channel level, not in every waveform.

### Symbol construction

`rmt_symbol_word_t` stores two level/duration pairs. Durations are 15-bit values, so any duration greater than 32767 us must be split across multiple symbols with the same level.

Implement helpers:

```cpp
void append_mark(std::vector<rmt_symbol_word_t>& out, uint32_t us);
void append_space(std::vector<rmt_symbol_word_t>& out, uint32_t us);
void append_mark_space(std::vector<rmt_symbol_word_t>& out,
                       uint32_t mark_us, uint32_t space_us);
```

Rules:

- Mark = level 1 for `mark_us`.
- Space = level 0 for `space_us`.
- Split durations recursively/iteratively into chunks <= 32767 us. A 110000 us LED trailing gap becomes four symbols, for example 32767 + 32767 + 32767 + 11700.
- Keep marks and spaces as separate symbols initially; do not merge across level changes unless tests prove it is safe.
- Build into a reused `std::vector` or fixed-size array on the IR task to avoid heap fragmentation.

Example transmit:

```cpp
rmt_transmit_config_t tx_cfg = {};
tx_cfg.loop_count = 0;

for (uint8_t i = 0; i < repeat; ++i) {
    ESP_ERROR_CHECK(rmt_transmit(tx, encoder, symbols.data(),
                                 symbols.size() * sizeof(symbols[0]), &tx_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx, 1000));
}
```

`repeat` is part of the IR request. The Python default is `2` for `ir_send_in_thread`, while sequence calls that pass `1` must remain single transmissions. Do not add an extra gap between repeats: the waveform already contains the protocol's trailing gap or signal-free time.

### Protocol encoder

Port the two paths of `ir_sling()` directly.

#### Standard space-encoded protocols

For devices `BEAM`, `LED`, `TV`, and `DVD`:

1. Emit leading mark `leading_pulse_duration`.
2. Emit leading space `leading_gap_duration`.
3. For each code character:
   - `'0'`: mark `zero_pulse`, space `zero_gap`.
   - `'1'`: mark `one_pulse`, space `one_gap`.
4. If `send_trailing_pulse`: mark `one_pulse`.
5. If `trailing_gap`: append that space.
6. Transmit the whole waveform `repeat` times.

Important details:

- `BEAM` and `LED` have different zero/one gaps; do not reuse the TV timing.
- `DVD` has `send_trailing_pulse = false` and a 14400 us trailing gap.
- `TV` has a zero trailing gap in the current implementation, even though the comment lists a repeat gap. Keep the code behavior, not the comment.
- The longest gap, the LED's 110000 us, must be split.

#### RC6 for the soundbar (`BAR`)

Use the Python algorithm exactly:

- `T = 444` us.
- Header: mark `6T`, space `2T`.
- Start bit: mark `T`, space `T`.
- Three mode bits: for each bit, space `T`, then mark `T`.
- Trailer/toggle bit:
  - toggle value 0: space `2T`, then mark `2T`.
  - toggle value 1: mark `2T`, then space `2T`.
- Manchester code:
  - `'0'`: mark `T`, space `T`.
  - `'1'`: space `T`, mark `T`.
- Signal-free time: space `6T` (2664 us).
- Transmit the waveform `repeat` times.

The `toggle_bit` is flipped once per BAR **command**, before encoding, and is shared by the repeated copies of that command. Protect it with the IR worker's ownership; do not flip it from multiple tasks.

### Device timing table

Port the code tables from `ir_hub.py:448-672` verbatim into `ir_codes.cpp`. The critical parameters are:

| Device | Frequency | Bits | Header mark/space (us) | Zero mark/space (us) | One mark/space (us) | Trailing pulse | Trailing gap (us) |
|---|---:|---:|---:|---:|---:|---:|---:|
| `BAR` | 36000 | RC6 (21-bit flags) | RC6, `T=444` | — | — | RC6 algorithm | 6T free time |
| `BEAM` | 38000 | 32 | 9044 / 4428 | 619 / 507 | 619 / 1629 | yes (619) | 10000 |
| `LED` | 38000 | 32 | 9067 / 4439 | 604 / 519 | 604 / 1645 | yes (604) | 110000 |
| `TV` | 38000 | 32 | 9000 / 4500 | 619 / 507 | 619 / 1621 | yes (619) | 0 |
| `DVD` | 38000 | 12 data + 8 post | 2448 / 552 | 640 / 557 | 1240 / 557 | no | 14400 |

The source also documents the original analyzer readings in comments. Keep those comments next to the tables; they are useful when a real device stops responding.

The `BAR` table contains all keys used by the state machine, including `POWER`, inputs, volume, tone, surround, and sync. The `BEAM`, `LED`, `TV`, and `DVD` tables contain every key referenced by the maps in `ir_hub.py`.

### IR request API

```cpp
enum class IrDevice : uint8_t { Bar, Beam, Led, Tv, Dvd, FireTvBeam, Unknown };

struct IrRequest {
    IrDevice device;
    char command[24];
    uint8_t repeat = 2;
};

bool ir_submit(IrDevice device, const char* command, uint8_t repeat = 2);
bool ir_submit_name(const char* full_name, uint8_t repeat = 2);
```

`ir_submit_name()` splits on the first whitespace exactly like Python:

```cpp
device, command = button_name.strip().upper().split()
```

It must accept names such as `BAR VOL+`, `TV HDMI2`, `LED POWERON`, and `FIRETVBEAM 103`. Unknown names and names containing `UNKNOWN` are ignored, matching `ir_send()`.

`FIRETVBEAM` is not an IR device: route it to the ADB command queue.

### IR concurrency and timing

- The IR worker alone owns the RMT channel.
- `ir_send_in_thread()` becomes a non-blocking `ir_submit()`; it must not block the control task.
- Use a FIFO queue. The Python code uses a mutex but thread scheduling does not guarantee request order; FIFO is an intentional improvement that makes the critical `TV POWEROFF` first-send deterministic.
- Long sequences such as `switch_bar_on()` must not sleep on the IR task. Implement them in a sequence task or with `esp_timer` one-shot callbacks that submit each IR request at the correct time.
- Do not hold the control queue while sleeping. The Python code blocks its press thread during `time.sleep()` but accepts other presses; keep the ESP32 equivalent non-blocking or explicitly serialize sequences while still allowing the required state checks.

IR self-test firmware should first emit a known TV `POWER` command. If the TV responds, the carrier, pin, and waveform order are correct before testing the soundbar RC6 path.

Bluetooth PS3 BD remote
-----------------------

### Host stack

Use Bluetooth Classic with Bluedroid and the HID Host profile:

```cpp
esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM)); // BR/EDR + BLE
ESP_ERROR_CHECK(esp_bluedroid_init());
ESP_ERROR_CHECK(esp_bluedroid_enable());

esp_hidh_config_t hidh_cfg = {};
hidh_cfg.callback = hidh_callback;
hidh_cfg.event_stack_size = 4096;
ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));
```

Enable HID Host in `sdkconfig.defaults` and keep coexistence in mind: Wi-Fi must remain usable while the remote is connected. Use `CONFIG_SW_COEXIST_ENABLE=y`, keep Wi-Fi power save at a sensible level, and test ADB throughput with the remote connected.

The PS3 BD remote is already paired with the Pi. For ESP32 bring-up:

1. Scan for the configured MAC (`00:1E:3D:10:CB:F0` by default).
2. Try `esp_hidh_dev_open()` with the Classic transport and register for HID input reports.
3. If the generic host cannot open the device or does not deliver reports, switch to a raw L2CAP HID-host implementation modeled on `jvpernis/esp32-ps3` and the ESP-IDF 5.5.2 Fire TV bridge referenced below.
4. Store the Bluetooth link key if the stack exposes it. PS3-era devices may use a fixed PIN such as `0000`; use `esp_bt_gap_set_pin(..., ESP_BT_PIN_TYPE_FIXED, 4, pin)` only as a fallback and verify pairing on real hardware.
5. Do not require the Pi to remain powered. The ESP32 must be able to reconnect after a reboot and after the deliberate 30-minute disconnect.

### Report parsing

The Python code currently reads raw HCI ACL packets and detects the PS3 BD remote report by these fields:

- ACL packet type `0x02`.
- Last five payload bytes equal `FF FF FF FF FF`.
- Battery value in the low nibble of one report byte, assumed `0..5`.
- Button-up condition: a byte equal to `255` combined with another byte equal to `0`.
- Button value decoded as a big-endian integer from the report button bytes.

The Linux `hid-sony.c` PS3 BD remote layout is the reference for the HID report:

- Byte 0: controller number.
- Bytes 1-3: joypad/button bitmap.
- Byte 4: remote button.
- Bytes 5-9: `0xFF`.
- Byte 10: press indication.
- Byte 11: battery level `0..5`.

Treat these offsets as the starting point, not as a substitute for a real capture. On first bring-up, log the first 16 bytes of each HID input report and compare them with the known layout. Include the report ID if the HID host includes it in the callback payload; do not mix a report-ID-prefixed buffer with the Python offsets.

Port `playstation_map` and `playstation_convert` from `ir_hub.py:31-41` verbatim. The full scancode table is required, especially the large values:

```text
UP 268435540, DOWN 1073741910, LEFT 2147483735, RIGHT 536870997,
OK 2059, OPTIONS 1048668, BACK 2097245, START 134217811
```

Preserve `AUIDO` exactly. The conversions that matter to the rest of the system are:

```text
EJECT -> POWER        L1 -> LIGHT          AUIDO -> MUTE
POPUP -> MENU         SLOWREWIND -> SOURCE SLOWFORWARD -> SELECT
OPTIONS -> VOL+       VIEW -> VOL-         BACK -> CH+
X -> CH-              TOPMENU -> LIST      PLAYSTATION -> HOME
```

### Repeat and release handling

`repeat_key` is:

```text
VOL+, VOL-, CH+, CH-, UP, DOWN, LEFT, RIGHT
```

The Python socket starts without a timeout. On the first press of a repeatable key it changes the read timeout to 700 ms, and after the first timeout to 200 ms. Each timeout produces another button press until a button-up packet arrives. Reproduce this with a per-remote state machine:

- On button-down: enqueue one press immediately.
- If the key is repeatable: start a 700 ms timer, then a repeating 200 ms timer.
- On button-up or disconnect: cancel the timers and reset to the non-repeating state.
- Do not generate a press for the release report.
- If the report protocol does not provide a clean release indication, infer release from the button bitmap becoming empty. Validate with a real remote before relying on inference.

### Battery and sleep

The Python code tracks `ps3_battery_level` `0..5`, assumes `5` at startup, and uses it only for the cold-start indication. Keep that behavior.

Reset a 1800 s inactivity timer on every received button. When it expires, disconnect the remote so it can sleep and save battery. Replace `bluetoothctl disconnect` with the HID host close API or `esp_bt_gap_disconnect()` using the configured MAC. Reconnect on the next button press if the remote supports wake-by-button; this must be verified on hardware because the original Pi relies on the BlueZ/remote reconnect behavior.

RGB LED
-------

The Python code writes exactly three bytes over SPI:

```python
spi.open(bus=0, device=0)
spi.mode = 0
spi.max_speed_hz = 1000000
spi.writebytes(rgb_leds)   # [R, G, B]
```

That is consistent with a WS2801-style LED. Implement:

- SPI host `SPI2_HOST` (or `SPI3_HOST` depending on the board).
- SCLK GPIO 18, MOSI GPIO 23, no MISO, CS unused (`SPI_CS0` can be left unconnected).
- Mode 0, 1 MHz, MSB-first, 8-bit words.
- A three-byte transaction for `{R, G, B}`.
- A WS2801 latch interval: hold the data line low for at least 500 us after the transaction. Verify whether the existing LED needs this; the Python code relies on the SPI device close/CS behavior and may already provide enough delay.

If the LED is actually WS2812/NeoPixel, three raw SPI bytes will not work. In that case add a WS2812 RMT encoder and change only `rgb_led.cpp`; keep the `set_rgb(Rgb)` API unchanged.

`set_rgb()` in Python intentionally swallows all errors. On ESP32, log at debug level but do not crash the control task if the LED is absent. The LED state update must not delay IR or Bluetooth processing.

State colors are fixed:

| State | RGB |
|---|---|
| `PROJECTOR` | `[0, 0, 10]` |
| `TV` | `[0, 10, 0]` |
| `LIGHT` | `[10, 10, 10]` |
| `DVD` | `[10, 10, 0]` |
| `SWITCH` | `[10, 0, 10]` |
| `IDLE` | `[0, 0, 0]` |

When `alt_func` is active and the state is not `IDLE`, the LED is forced to `[255, 255, 255]` (white at maximum brightness). Otherwise every button press ends by applying the table color.

Fire TV ADB
----------

### Connection

The Python program shells out to `adb connect <host>:5555`, then keeps a persistent `adb shell` process. Implement the ADB transport directly over TCP:

- Connect to the configured Fire TV IPv4 address or hostname on port `5555`.
- Send `CNXN` with protocol version `0x01000000`, a sensible max payload (4096 initially, 256 KiB after validation), and payload `host::\0`.
- Handle `AUTH` token challenges with an RSA key.
- Open the `shell:` service after `CNXN` succeeds.
- Send shell commands as `WRTE` payloads and drain `OKAY`/`WRTE` responses.
- Reconnect every 5 seconds when disconnected, matching the Python loop.
- Use a single ADB task and a queue of commands. Do not run concurrent shell writes.

ADB packet framing is little-endian:

```text
command, arg0, arg1, data_length, data_checksum, magic
magic = command ^ 0xFFFFFFFF
```

Commands needed for this application:

| Command | Meaning |
|---|---|
| `CNXN` | Connect/version negotiation. |
| `AUTH` | Authentication challenge/response. |
| `OPEN` | Open `shell:` or another service. |
| `OKAY` | Acknowledgement. |
| `WRTE` | Write shell input. |
| `CLSE` | Close stream. |

### Authentication

Use RSA 2048 and the Android ADB public-key encoding. The public key sent to the Fire TV must be in the exact binary format expected by the ADB daemon; the AOSP `android_pubkey.cpp` encoder is the reference.

Recommended provisioning order:

1. Generate an RSA key pair on the ESP32 with mbedTLS and store it in NVS. This avoids compiling a private key into the firmware.
2. On first connection, send the public key and wait for the Fire TV authorization dialog. The user accepts it once.
3. Persist the key across reboots. Use NVS encryption and/or flash encryption for a production device; plain NVS is not a secure secret store.
4. For temporary bring-up only, convert the existing Pi `~/.android/adbkey` and provision it through a local debug path. Never commit the private key to the repository.

Match the AOSP signature scheme used by the Fire TV ADB version and test both first-time authorization and reconnect after a reboot. If the TV rejects the key, inspect whether the daemon expects SHA-1 or SHA-256 PKCS#1 v1.5 signing before changing the key format.

### Shell commands

Port `send_keyevent()` exactly, including its special cases:

```text
HOME -> input keyevent 3
1    -> am start -n de.swr.ard.avp.mobile.android.amazon/.TvActivity
2    -> am start -n org.xbmc.kodi/.Splash
3    -> am start -n org.smarttube.stable/com.liskovsoft.smartyoutubetv2.tv.ui.main.SplashActivity
4    -> am start -n com.amazon.firetv.youtube/dev.cobalt.app.MainActivity
```

All other commands use the Fire TV input device:

```text
sendevent /dev/input/event1 1 <keycode> 1 &&
sendevent /dev/input/event1 0 0 0 &&
sendevent /dev/input/event1 1 <keycode> 0 &&
sendevent /dev/input/event1 0 0 0
```

The Python code concatenates the last two lines without a space after `&&`; the shell accepts that. Preserve the effective command sequence, but add a trailing newline so the shell executes it.

A command submitted while ADB is disconnected is dropped by the Python implementation. Preserve that behavior initially rather than buffering stale remote presses. Log the drop at info/debug level.

Radio socket
------------

The Python code performs:

```python
requests.get("http://radiosocket/cm", params={"cmnd": f"Power {state}"})
```

This becomes effectively:

```text
http://radiosocket/cm?cmnd=Power%20On
http://radiosocket/cm?cmnd=Power%20Off
```

Implement with `esp_http_client`:

- Resolve the configured `radiosocket` hostname through mDNS or use a static IPv4 address.
- Set a short timeout, for example 3 seconds.
- Discard the response body.
- Never fail the control task if the socket is offline; log and continue.

Timers:

- After `POWER` handling, arm a 1800 s one-shot timer to send `Power Off`.
- When entering any non-`POWER` state from `IDLE`, send `Power On` unconditionally.
- If the off timer was still alive, cancel it instead of showing the cold-start battery indication.
- If the timer was not alive, this is a cold start: show the battery indication before continuing.

Preserve the original order: the radio socket is switched on before the six-second cold-start indication, and the timer is armed only after the `POWER` branch finishes.

Button and state model
----------------------

### State and map definitions

Use an enum internally and keep the exact external names for logs and script input:

```text
IDLE, PROJECTOR, TV, LIGHT, DVD, SWITCH
```

```cpp
state_map = {
    {"1", PROJECTOR}, {"2", TV}, {"3", LIGHT}, {"4", DVD},
    {"5", SWITCH}, {"POWER", IDLE}
};
```

Preserve `state_rgb`, `repeat_key`, `map_TV`, `map_DVD`, `map_PROJECTOR`, `map_LIGHT`, and `map_select` from `ir_hub.py:25-79`. They are data, not behavior; put them in a dedicated file and unit-test the lookups.

The complete routing tables contain the intentional aliases:

- `DISPLAY` is treated as mute in TV/DVD/projector modes.
- `SOURCE` on the projector/DVD maps to `BEAM SOURCE`.
- `R1` maps to `BEAM OK`.
- `R2`/`R3` control soundbar surround in projector mode.
- `map_select` overrides the normal map while `alt_func` is true.
- `TEXT`, `HOLD`, `REC`, and the color keys exist only in the select map or device-specific maps; do not prune them merely because the PS3 BD remote may not send every key.

All unknown lookups resolve to the string `UNKNOWN` and are ignored by the IR sender.

### Processing order

`on_button_press()` is protected by `press_lock` in Python. Implement the equivalent by consuming a single control queue in one task. The order inside the handler must be preserved:

1. **SELECT handling:** if the button is `SELECT` and the current state is not `IDLE`, toggle `alt_func`.
2. **LIGHT handling:**
   - In `IDLE`, replace the button with `3`.
   - In `LIGHT`, replace the button with `POWER`.
   - Otherwise, toggle the LED strip power with one IR transmission:
     - if `led_is_on`: `LED POWEROFF`, repeat 1.
     - else: `LED POWERON`, repeat 1.
     - then flip `led_is_on`.
3. **Number override:** if the current state is `LIGHT` or `SWITCH` and the button is in `state_map`, set `alt_func = true`.
4. **State transition test:** if `(alt_func || button == "POWER") && button in state_map`, run the transition branch. Otherwise send the normal mapping.
5. **LED update:** if `alt_func && state != IDLE`, set white `[255,255,255]`; otherwise set the state color.

Do not reorder steps 1-3. In particular, the LIGHT special case changes the button before the state-map test, and the LIGHT/SWITCH override restores `alt_func`.

### Cold-start behavior

The transition branch is entered from `IDLE` for every button except `POWER`. Before the device-specific actions:

1. Send radio socket `Power On` unconditionally.
2. If the radio off timer is alive, cancel it. This is a warm start, so do not show the battery indication.
3. Otherwise this is a cold start:
   - Battery level `< 3`: repeat six times: set RGB `[255,0,0]`, wait 0.5 s, set RGB `[0,0,0]`, wait 0.5 s. The comments call this red at maximum power.
   - Battery level `< 4`: set RGB `[255,0,255]` and wait 6 s. The source comment calls this yellow, but the actual value is magenta.
   - Else: set RGB `[0,0,255]` and wait 6 s. The source comment calls this green, but the actual value is blue.
4. Then run the selected transition.

Keep the actual RGB values, not the comments, unless the hardware is deliberately corrected.

### Transition branches

These branches use the **old** state for their conditions. The state is changed only after the branch completes. Preserve this; it is essential for the "help action" retry behavior.

#### POWER

```text
mapping = none
alt_func = true
led_is_on = false

IR: TV POWEROFF
if old state is TV or SWITCH: sleep 3 s
if old state is PROJECTOR, DVD, TV, or SWITCH: IR BAR POWER
if old state is PROJECTOR or DVD: start projector-off sequence
IR: DVD POWEROFF
IR: LED POWEROFF
reset radio socket timer and arm Power Off after 1800 s
```

`TV POWEROFF` must be submitted first to avoid IR interference. The 3 s delay is required when the TV was on. `BAR POWER` is sent even if the soundbar may not be on; the source comment notes that an incorrect toggle switches off after 15 minutes idle.

#### Button `1` (PROJECTOR)

```text
mapping = map_PROJECTOR
alt_func = false
led_is_on = false

IR: TV POWEROFF
if old state is TV or SWITCH: sleep 3 s
start soundbar-on sequence with input BLUETOOTH
if old state is not DVD: start projector-on sequence with input PROJECTOR/HDMI1
if old state is DVD: do nothing (the input-select call is commented out)
IR: LED POWEROFF
IR: DVD POWEROFF
```

The projector helper currently sends only `BEAM POWER`; the HDMI input select is intentionally commented out in Python. Do not "fix" this without confirming the projector's behavior.

#### Buttons `2` and `5` (TV/SWITCH)

```text
mapping = map_TV
alt_func = false
led_is_on = true

IR: LED POWEROFF
if old state is PROJECTOR or DVD:
    start projector-off sequence
    sleep 3 s
IR: DVD POWEROFF

if button == "2":
    if old state is SWITCH: IR TV CH+ (repeat 1)
    else: start switch_tv_on("TV", "CH+")
if button == "5":
    if old state is TV: IR TV HDMI2 (default repeat 2)
    else: start switch_tv_on("SWITCH", "HDMI2")

start soundbar-on sequence with input OPTICAL
```

Button `5` shares the `map_TV` mapping even though its state is `SWITCH`.

#### Button `3` (LIGHT)

```text
mapping = map_LIGHT
alt_func = false
led_is_on = true

IR: TV POWEROFF
if old state is TV or SWITCH: sleep 3 s
if old state is PROJECTOR, DVD, TV, or SWITCH: IR BAR POWER
IR: LED POWERON
if old state is PROJECTOR or DVD: start projector-off sequence
IR: DVD POWEROFF
IR: LED WHITE
start led_max_brightness sequence
```

#### Button `4` (DVD)

```text
mapping = map_DVD
alt_func = false
led_is_on = false

IR: TV POWEROFF
if old state is TV or SWITCH: sleep 3 s
IR: DVD POWERON
start soundbar-on sequence with input BLUETOOTH
if old state is not PROJECTOR: start projector-on sequence with input DVD/HDMI2
if old state is PROJECTOR: do nothing
IR: LED POWEROFF
```

#### End of any transition

```text
prev_state = state
state = state_map[button]
```

Before running the branch, the source checks:

```text
if old state == state_map[button]:
    state = prev_state
```

This is the "help action requested -> do transition again" behavior. Implement it in the same place: pressing the number of the current state restores `prev_state` before the branch, so the transition is executed from the previous mode rather than being a no-op.

### Normal mapping path

If the transition test is false:

```text
if alt_func:
    send map_select[button] (default UNKNOWN, repeat 1)
else if mapping != none:
    send mapping[button] (default UNKNOWN, repeat 1)
else:
    send nothing
```

`UNKNOWN` is ignored by the IR/ADB sender. The mapping remains in effect until another state transition.

### Long-running sequences

Implement each Python helper as a non-blocking sequence job that submits IR requests at the documented times.

#### `switch_tv_on(cur_state, input)`

```text
submit TV POWERON (default repeat 2)
wait 5 s
if state == cur_state: submit TV <input> repeat 1
wait 10 s
if state is TV or SWITCH: submit TV POWERON (default repeat 2)
```

The state checks use the live state, not the state at sequence start. The sequence must abort the input-select step if the user changed modes during the first 5 s.

#### `switch_projector_on_with_input_select(cur_state, input)`

```text
submit BEAM POWER
```

The input-select code is commented out in Python. The `cur_state` and `input` parameters are retained for compatibility with the call sites.

#### `switch_projector_off()`

```text
repeat 7 times:
    submit BEAM POWER repeat 1
    wait 0.1 s
```

#### `switch_bar_on(input)`

```text
submit BAR <input> (default repeat 2)
wait 15 s
submit BAR SURROUNDON (default repeat 2)
```

This sequence does not abort if the state changes; preserve that behavior.

#### `led_max_brightness()`

```text
repeat 10 times:
    submit LED BRIGHTER repeat 1
    wait 0.3 s
repeat 3 times:
    submit LED DIMMER repeat 1
    wait 0.3 s
```

Use `esp_timer` or a dedicated sequence task for the waits. Do not sleep in the control queue consumer if that would block new button events longer than the Python program does. If exact parity is preferred initially, run each sequence in its own FreeRTOS task and use `vTaskDelay()`; this mirrors the Python thread model most closely.

### Script command socket

The Pi exposes `/tmp/ir_hub_script.sock` and is driven by:

```bash
echo -n "$1" | sudo socat - UNIX-CONNECT:/tmp/ir_hub_script.sock
```

On the ESP32, implement a line-oriented TCP server on a configurable port (default `3333`), and optionally a UART command input. For each received line:

1. Strip leading/trailing whitespace.
2. Enqueue the exact button name as a `ScriptSocket` event.
3. Process it through the same `on_button_press()` path as Bluetooth input.

Example host-side replacement:

```bash
printf '%s\n' "$1" | socat - TCP:irhub.local:3333
```

Do not expose the command server to the public internet. It has no authentication in the original design. If the LAN is untrusted, add a shared-secret line prefix or bind it only to a dedicated control interface.

### Startup order

`app_main.cpp` should perform this sequence:

1. Initialize NVS and logging.
2. Load configuration (Wi-Fi, Fire TV address, radio socket address, optional PS3 MAC).
3. Initialize the RGB driver, set the initial `IDLE` color `[0,0,0]`.
4. Initialize the IR worker and RMT channel.
5. Start the ADB reconnect task.
6. Start the PS3 remote host and connection task.
7. Start the script command server.
8. Start the control task and its queue.
9. Optionally enable the task watchdog; feed it from long-running tasks.

The Python program starts `pigpiod` before the threads; on ESP32 the equivalent is successful RMT/SPI initialization before any IR or LED request is accepted.

Implementation phases
---------------------

### Phase 0: Hardware and behavior capture

- Identify the RGB LED: confirm WS2801 versus WS2812 by inspecting the LED/driver and measuring the existing SPI signals.
- Confirm the IR output driver polarity, transistor stage, supply, and whether all four diodes are wired in parallel.
- Capture the existing Pi behavior with a logic analyzer or IR receiver:
  - One TV `POWER` waveform.
  - One soundbar `VOL+` waveform (RC6).
  - One LED `POWERON` waveform (long trailing gap).
  - The first 16 bytes of a PS3 BD remote report for several buttons.
- Confirm the Fire TV ADB key already authorized and whether a freshly generated key can be authorized.
- Record the radio socket hostname/IP and the Fire TV IP/hostname.

Exit criterion: hardware and protocol facts are known, and the expected waveforms are stored as test fixtures.

### Phase 1: ESP-IDF skeleton and RGB

- Create `esp32/` with an ESP-IDF v5.5.x project targeting `esp32`.
- Add configuration for pins, Wi-Fi, Fire TV, radio socket, and optional PS3 MAC.
- Implement `rgb_led.cpp` with SPI mode 0, 1 MHz, three-byte writes, and latch delay.
- Implement a minimal control queue and script TCP server.
- Verify `POWER`, `1`-`5`, and `LIGHT` command strings can be injected without a remote.

Exit criterion: the RGB LED shows the six state colors, white in select mode, and the script socket can drive a fake state machine.

### Phase 2: Standard IR protocols over RMT

- Implement the symbol builder and duration splitting.
- Port the `BEAM`, `LED`, `TV`, and `DVD` timing/code tables.
- Implement `ir_submit_name()` and the IR worker.
- Add a debug command to transmit a selected key with a selected repeat count.
- Compare generated symbol durations against a Python host fixture derived from `ir_sling()`.
- Test on the real TV first, then projector, LED strip, and DVD player.

Exit criterion: TV power and input selection work reliably; the projector and LED strip respond; no watchdog resets or RMT transfer errors.

### Phase 3: RC6 soundbar

- Implement the RC6 encoder and toggle-bit state.
- Port the complete `BAR` key table.
- Verify repeated commands and the 6T signal-free time.
- Compare a captured `VOL+` waveform with the Pi capture.

Exit criterion: soundbar power, input select, volume, mute, surround, tone, and sync keys work, including repeated volume presses.

### Phase 4: Button routing and state machine

- Port all lookup tables and the exact processing order.
- Implement the cold-start battery indication and radio timer.
- Implement the long-running sequences with live state checks.
- Port LED brightness and toggle behavior.
- Add host tests for every transition from every old state, including the same-state retry.

Exit criterion: script-injected button sequences reproduce the Pi behavior, including timing, radio socket calls, and state colors.

### Phase 5: PS3 BD remote

- Implement the HID host connection and report decoding.
- Port the scancode map, conversions, repeat logic, battery tracking, and inactivity disconnect.
- Add raw-report logging behind a debug flag.
- Test with a real remote from a cold boot, after 30 minutes idle, after Wi-Fi traffic, and after reconnect.

Exit criterion: all remote buttons map correctly, autorepeat works for the required keys, battery indication is correct, and the remote reconnects after the deliberate sleep disconnect.

### Phase 6: Fire TV ADB

- Implement ADB framing, key authentication, `shell:`, and reconnect.
- Provision the RSA key and authorize it once on the Fire TV.
- Port all keyevent and app-launch commands.
- Test navigation, HOME, app shortcuts, and recovery after the Fire TV reboots or changes network state.

Exit criterion: projector-mode Fire TV navigation works with the same latency as the Pi, and the ESP32 reconnects automatically after a Fire TV reboot.

### Phase 7: Integration and hardening

- Enable Wi-Fi/Bluetooth coexistence testing with the remote connected and ADB active.
- Add task watchdog coverage, bounded queues, and failure logging.
- Add OTA update support if desired; the original Pi uses `systemd` restart, not OTA.
- Add a documented provisioning procedure for Wi-Fi, Fire TV address, radio socket, and ADB key.
- Run a soak test for at least 24 hours with periodic remote presses and ADB commands.

Exit criterion: no memory leaks, no stuck IR queue, no missed remote presses, and stable reconnect behavior.

Build and test commands
-----------------------

```bash
. "$IDF_PATH/export.sh"
cd /home/corrados/mypitools/projects/ir_hub/esp32
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Recommended `sdkconfig.defaults` starting points:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_HID_HOST_ENABLED=y
CONFIG_SW_COEXIST_ENABLE=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=4096
```

Host-side logic tests should not require ESP-IDF. Build them with plain CMake or a small test harness. If no framework exists in the repository, use a minimal assertion-based test binary first; do not add a large test dependency merely for this project.

Minimum test matrix:

- Standard encoder: header, zero, one, trailing pulse, and split gap for every device.
- RC6 encoder: mode bits, both toggle states, and Manchester `0`/`1`.
- IR name parsing: valid names, lowercase input, extra whitespace, unknown device, `UNKNOWN`, `FIRETVBEAM` routing.
- Key maps: all referenced keys resolve; no accidental removal of the `AUIDO` typo.
- State machine: every button in every old state, including `SELECT`, `LIGHT`, same-state retry, and `alt_func`.
- Timing sequences: `switch_tv_on`, `switch_projector_off`, `switch_bar_on`, and `led_max_brightness` submit the expected ordered requests.
- PS3 report decoding: known raw reports from the hardware capture, button-up, and battery values.
- ADB packet framing and command generation, including the exact shell strings.
- Radio socket URL encoding and timer cancellation.

Verification against the Pi
---------------------------

For parity, compare:

- The first 10 s after each state transition.
- The order and repeat count of every IR request.
- The 3 s, 5 s, 6 s, 10 s, 15 s, and 0.3 s delays.
- The radio socket on/off timing.
- The LED state after every press.
- Repeated volume/CH/arrow presses and their 700 ms/200 ms cadence.
- Battery indication at levels 0-2, 3, and 4-5.

The IR waveform comparison should allow RMT's 1 us resolution; the protocol tolerances are much wider. Do not require the ESP32 waveform to be byte-for-byte identical to `pigpio`; require correct decoder behavior on the real devices.

Risks and mitigations
---------------------

| Risk | Mitigation |
|---|---|
| Bluetooth Classic HID host does not expose the PS3 BD remote reports. | Keep the raw L2CAP fallback and validate early in Phase 5. Capture raw reports before building the state machine around them. |
| Wi-Fi/Bluetooth coexistence drops remote presses or ADB packets. | Enable software coexistence, keep ADB traffic small, test both active simultaneously, and use separate priority/task design. |
| The PS3 remote does not reconnect after the 30-minute HID disconnect. | Test the exact disconnect/reconnect behavior early. If wake-by-button does not work, retain the connection or disconnect only when the user requests standby. |
| ADB authentication fails or the key is lost. | Implement the exact AOSP signing/key format, persist the key in encrypted NVS, and test first-time authorization and reboot. Keep a recovery path to re-authorize. |
| Long IR gaps are truncated by the RMT encoder. | Split all durations >32767 us and unit-test the 110000 us LED gap and 107636 us BAR gap. |
| RC6 toggle bit causes intermittent soundbar failures. | Centralize toggle handling in the IR worker and test repeated presses and repeated waveforms. |
| RGB LED is WS2812, not WS2801. | Verify before wiring; keep the `set_rgb()` API and swap only the driver implementation. |
| IR diode drive is insufficient or overheats. | Reuse the existing transistor/driver stage; verify current and duty cycle; do not drive diodes directly from GPIO. |
| NVS contains Wi-Fi/ADB secrets in plaintext. | Use NVS encryption and/or flash encryption for production; never commit credentials or private keys. |
| 24/7 operation leaks memory or queues. | Use fixed-size queues, avoid per-event heap allocation, and run a 24-hour soak test. |

Open questions
--------------

1. Is the RGB LED definitely WS2801-compatible, and does it require an explicit latch interval?
2. What is the actual IR driver polarity and current capability with four diodes?
3. Should the script socket be TCP only, or is UART/serial preferred for the replacement Pi-to-ESP32 workflow?
4. Should the ESP32 use a device-generated ADB key, or must it reuse the existing Pi key for a seamless first migration?
5. Is OTA update required, or is USB flashing acceptable?
6. Should the 30-minute PS3 remote disconnect be retained exactly, or can the remote stay connected when powered?
7. What Wi-Fi provisioning method is preferred: compile-time `menuconfig`, a provisioning AP, or a file in NVS?
8. Does the Fire TV accept `sendevent /dev/input/event1` on the current firmware, or is the event device number different?

Answer these before implementation reaches the affected phase; none block the IR/RGB skeleton.

Sources and tools consulted
---------------------------

- ESP-IDF Bluetooth HID Host API: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_hidh.html>
- ESP-IDF HID host example: <https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/esp_hid_host>
- `jvpernis/esp32-ps3`: raw PS3 HID/L2CAP reference and fallback: <https://github.com/jvpernis/esp32-ps3>
- `Hokl3d/ESP32-FireTV-Remote-Bridge`: ESP-IDF 5.5.2 raw Bluetooth HID and Fire TV bridge reference: <https://github.com/Hokl3d/ESP32-FireTV-Remote-Bridge>
- Linux `hid-sony.c`: PS3 BD remote report layout and key mappings: <https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c>
- ESP-IDF RMT documentation: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/rmt.html>
- ESP-IDF RMT IR NEC transceiver example: <https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/ir_nec_transceiver>
- `IRremoteESP8266`: useful protocol/timing reference, not the recommended ESP-IDF runtime encoder: <https://github.com/crankyoldgit/IRremoteESP8266>
- ESP-IDF SPI master documentation: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_master.html>
- ESP-IDF HTTP client documentation: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_http_client.html>
- ESP-IDF timers: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html>
- ESP-IDF Wi-Fi/Bluetooth coexistence: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/coexist.html>
- AOSP ADB protocol: <https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/protocol.txt>
- AOSP Android public-key encoding: <https://android.googlesource.com/platform/system/core/+/refs/heads/main/libcrypto_utils/android_pubkey.cpp>

Summary of research conclusions
-------------------------------

- The original ESP32 is the correct target; ESP32 variants without Bluetooth Classic cannot run the PS3 BD remote path.
- ESP-IDF plus C++ is the lowest-risk path because the needed Bluetooth Classic HID, RMT carrier, SPI, HTTP, mbedTLS, and coexistence support all exist as first-party components.
- RMT carrier modulation removes the need to generate 26-28 us carrier periods in software and gives much better timing than bit-banging.
- The PS3 remote is the highest-risk subsystem. The generic HID host should be attempted first, but the raw L2CAP fallback is not optional until the remote is proven to work.
- The Fire TV ADB path is self-contained but authentication and key persistence need explicit design; shelling out to `adb` is not available on the ESP32.
- The RGB LED is very likely WS2801-style because the Python code sends exactly three raw bytes over SPI. This must be confirmed before finalizing the driver.
- Most of the application complexity is not IR transmission; it is preserving the state machine, sequencing, timers, and multi-source input behavior of `ir_hub.py`.
