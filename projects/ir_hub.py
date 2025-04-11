#!/usr/bin/env python3

# created with help of ChatGPT

import struct
import time
import threading

DEVICE_PATH = "/dev/input/event16"

# Scancode to readable button name for Elgato eyetv remote
scancode_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"CH+", 16:"GREEN", 17:"VOL-", 18:"OK",
19:"VOL+", 20:"YELLOW", 21:"CH-", 22:"BLUE", 23:"BACK_LEFT", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

# State
last_scancode = None
last_time = 0

with open(DEVICE_PATH, "rb") as f:
    while True:
        data = f.read(24)
        if not data:
            continue

        tv_sec, tv_usec, event_type, code, value = struct.unpack("llHHI", data)

        if event_type == 4 and code == 4: # MSC_SCAN
            scancode = value - 4539649
            button_name = scancode_map.get(scancode, f"UNKNOWN ({scancode})")

            if scancode != last_scancode or time.time() - last_time > 0.2 or button_name in {"VOL+", "VOL-"}:
                # New key pressed
                last_scancode = scancode
                print(f"Button pressed: {button_name}")

            # Update time
            last_time = time.time()

        elif event_type == 0: # EV_SYN
            # Sync event — mark last_time for idle detection
            last_time = time.time()

