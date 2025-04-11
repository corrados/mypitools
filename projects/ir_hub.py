#!/usr/bin/env python3

# created with help of ChatGPT

import struct
import time
import threading

DEVICE_PATH = "/dev/input/event2"

# Scancode to readable button name for Elgato eyetv remote
scancode_map = {
    4539667: "OK",
    4539668: "VOL+",
    4539666: "VOL-",
    4539664: "CH+",
    4539670: "CH-",
    4539649: "POWER",
    4539650: "MUTE",
    4539663: "RED",
    4539665: "GREEN",
    4539671: "BLUE",
    4539669: "YELLOW",
    4539651: "1",
    4539652: "2",
    4539653: "3",
    4539654: "4",
    4539655: "5",
    4539656: "6",
    4539657: "7",
    4539658: "8",
    4539659: "9",
    4539661: "0",
    4539662: "ENTER",
    4539660: "LAST",
    4539672: "BACK_LEFT",
    4539674: "BACK_RIGHT",
    4539673: "PLAY",
    4539675: "REWIND",
    4539677: "FORWARD",
    4539676: "L",
    4539678: "STOP",
    4539679: "TEXT",
    4539712: "REC",
    4539713: "HOLD",
    4539714: "SELECT"
}

# Keys that should repeat while held
repeatable_keys = {"VOL+", "VOL-", "CH+", "CH-"}

# State
last_scancode = None
last_time = 0
repeat_thread = None
stop_repeat = threading.Event()


def repeat_action(button_name):
    """Start repeating the button action while button is held"""
    time.sleep(0.5)  # Initial delay
    while not stop_repeat.is_set():
        # If time since last event is too long, consider the key released
        if time.time() - last_time > 0.3:
            break
        print(f"Repeating: {button_name}")
        time.sleep(0.1)


with open(DEVICE_PATH, "rb") as f:
    while True:
        data = f.read(24)
        if not data:
            continue

        tv_sec, tv_usec, event_type, code, value = struct.unpack("llHHI", data)

        if event_type == 4 and code == 4: # MSC_SCAN
            scancode = value
            button_name = scancode_map.get(scancode, f"UNKNOWN ({scancode})")

            if scancode != last_scancode:
                # New key pressed
                last_scancode = scancode
                print(f"Button pressed: {button_name}")

                # Stop previous repeat thread
                if repeat_thread and repeat_thread.is_alive():
                    stop_repeat.set()
                    repeat_thread.join()

                stop_repeat.clear()

                if button_name in repeatable_keys:
                    repeat_thread = threading.Thread(target=repeat_action, args=(button_name,))
                    repeat_thread.start()

            # Update time
            last_time = time.time()

        elif event_type == 0: # EV_SYN
            # Sync event — mark last_time for idle detection
            last_time = time.time()

