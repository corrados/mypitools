Port ../../ir_hub.py to ESP32
=============================

Current status
--------------

ir_hub.py currently runs stable on Raspberry Pi 3B+. An RGB LED and four IR transmitter diodes are connected.

Note that I do not use a standard PS3 controller but the PS3 BD remote which is like a normal TV remote but with bluetooth instead of IR.

Plan
----

ir_hub.py shall be ported to C++ to be run on an ESP32. The ESP32 is the original 2x Xtensa LX6 version (240 MHz dual core).

The setup requires three distinct subsystems running together: Bluetooth Classic (for the PS3 remote), Wi-Fi (for the Fire TV Stick), and RMT/GPIO PWM (for IR transmission).

Regarding IR transmitter: Raspberry Pi scripts often use native Linux libraries or daemon-based wrappers (like pigpio or direct memory-mapped GPIO) to blast precise infrared timings. The ESP32 handles peripherals completely differently, using hardware timers, RMT (Remote Control Transceiver) peripherals, or dedicated libraries like IRremoteESP8266.
