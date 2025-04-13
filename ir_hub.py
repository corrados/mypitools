#!/usr/bin/env python3

# created with help of ChatGPT

import threading
import struct
import time
import evdev

device_path = None
state       = "IDLE"
alt_func    = False
press_lock  = threading.Lock()
ir_lock     = threading.Lock()

# scancode to readable button name for Elgato EyeTV remote
scancode_offset = 4539649
scancode_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"CH+", 16:"GREEN", 17:"VOL-", 18:"OK",
19:"VOL+", 20:"YELLOW", 21:"CH-", 22:"BLUE", 23:"BACK_LEFT", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

# key mapping from EyeTV remote to other device remote
map_tv = {"CH+":"TV UP", "CH-":"TV DOWN", "VOL-":"TV LEFT", "VOL+":"TV RIGHT", "OK":"TV OK", "1":"TV 1", "2":"TV 2",
"3":"TV 3", "4":"TV 4", "5":"TV 5", "6":"TV 6", "7":"TV 7", "8":"TV 8", "9":"TV 9", "0":"TV 0", "BACK_LEFT":"TV MENU"}
map_dvd = {"CH+":"DVD UP", "CH-":"DVD DOWN", "VOL-":"DVD LEFT", "VOL+":"DVD RIGHT", "OK":"DVD OK", "1":"DVD 1", "2":"DVD 2",
"3":"DVD 3", "4":"DVD 4", "5":"DVD 5", "6":"DVD 6", "7":"DVD 7", "8":"DVD 8", "9":"DVD 9", "0":"DVD 0", "BACK_LEFT":"DVD MENU"}

def watch_input():
  (last_scancode, last_time) = (None, 0) # state
  with open(device_path, "rb") as f:
    while True:
      data = f.read(24)
      if data:
        _, _, event_type, code, value = struct.unpack("llHHI", data)
        if event_type == 4 and code == 4: # MSC_SCAN
          scancode = value - scancode_offset
          button_name = scancode_map.get(scancode, f"UNKNOWN ({scancode})")
          if scancode != last_scancode or time.time() - last_time > 0.2 or button_name in {"VOL+", "VOL-"}:
            last_scancode = scancode
            threading.Thread(target=on_button_press, args=(button_name,)).start()
          last_time = time.time()
        elif event_type == 0: # EV_SYN (sync event)
          last_time = time.time() # mark last_time for idle detection

def on_button_press(button_name):
  global state, alt_func
  with press_lock:
    print(f"Button pressed: {button_name}")
    if button_name == "SELECT":
      alt_func = True
      return
    match button_name:
      case "RED": # PROJECTOR -----
        if state in ("TV", "CONSOLE", "LIGHT"):
          threading.Thread(target=lambda: ir_send("LED POWER OFF")).start()
          if state in ("TV", "CONSOLE"):
            threading.Thread(target=lambda: ir_send("TV POWER OFF")).start()
        threading.Thread(target=lambda: ir_send("BEAM POWER ON")).start()
        state = "PROJECTOR"
      case "GREEN": # TV -----
        if state in ("PROJECTOR"):
          threading.Thread(target=lambda: ir_send("BEAM POWER OFF")).start()
        if not state in ("CONSOLE"):
          threading.Thread(target=lambda: ir_send("TV POWER ON")).start()
          threading.Thread(target=lambda: ir_send("LED POWER ON")).start()
        state = "TV"
      case "YELLOW": # LIGHT -----
        if state in ("TV", "CONSOLE"):
          threading.Thread(target=lambda: ir_send("TV POWER OFF")).start()
          # LIGHT is already on
        else:
          if state in ("PROJECTOR"):
            threading.Thread(target=lambda: ir_send("BEAM POWER OFF")).start()
          threading.Thread(target=lambda: ir_send("LED POWER ON")).start()
        state = "LIGHT"
      case "BLUE": # CONSOLE -----
        if state in ("PROJECTOR"):
          threading.Thread(target=lambda: ir_send("BEAM POWER OFF")).start()
        if not state in ("TV"):
          threading.Thread(target=lambda: ir_send("TV POWER ON")).start()
          threading.Thread(target=lambda: ir_send("LED POWER ON")).start()
        state = "CONSOLE"
    threading.Thread(target=lambda: ir_send(f"TV {map_tv.get(button_name, 'UNKNOWN')}")).start()
    alt_func = False # reset alternate function flag at the end of the function

def ir_send(button_name):
  with ir_lock:
    if not "UNKNOWN" in button_name:
      print(f"IR send {button_name}")

if __name__ == '__main__':
  target_device = None
  for device in [evdev.InputDevice(path) for path in evdev.list_devices()]:
    if "EyeTV" in device.name:
      target_device = device
  if target_device:
    device_path = target_device.path
    threading.Thread(target=watch_input).start()
  else:
    raise RuntimeError(f"Input device EyeTV not found.")

