#!/usr/bin/env python3

# created with help of ChatGPT

import threading
import struct
import time
import evdev

device_path = None
state       = "IDLE"
prev_state  = "IDLE"
mapping     = None
alt_func    = False
press_lock  = threading.Lock()
ir_lock     = threading.Lock()

state_map = {"RED":"PROJECTOR", "GREEN":"TV", "YELLOW":"LIGHT", "BLUE":"DVD", "POWER":"IDLE"}

# scancode to readable button name for Elgato EyeTV remote
scancode_offset = 4539649
scancode_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"CH+", 16:"GREEN", 17:"VOL-", 18:"OK",
19:"VOL+", 20:"YELLOW", 21:"CH-", 22:"BLUE", 23:"BACK_LEFT", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

# key mapping from EyeTV remote to other device remote
map_TV = {"CH+":"TV UP", "CH-":"TV DOWN", "VOL-":"TV LEFT", "VOL+":"TV RIGHT", "OK":"TV OK",
"1":"TV 1", "2":"TV 2", "3":"TV 3", "4":"TV 4", "5":"TV 5", "6":"TV 6", "7":"TV 7", "8":"TV 8",
"9":"TV 9", "0":"TV 0",
"BACK_LEFT":"TV MENU"}
map_DVD = {"CH+":"DVD UP", "CH-":"DVD DOWN", "VOL-":"DVD LEFT", "VOL+":"DVD RIGHT", "OK":"DVD OK",
"1":"DVD 1", "2":"DVD 2", "3":"DVD 3", "4":"DVD 4", "5":"DVD 5", "6":"DVD 6", "7":"DVD 7", "8":"DVD 8",
"9":"DVD 9", "0":"DVD 0",
"BACK_LEFT":"DVD MENU"}
map_PROJECTOR = {"VOL-":"BAR VOLUMEDOWN", "VOL+":"BAR VOLUMEUP", "MUTE":"BAR MUTE"}
map_LIGHT = {"CH+":"LED BRIGHTER", "CH-":"LED DIMMER", "VOL-":"LED DIMMER", "VOL+":"LED BRIGHTER",
"1":"LED BLUE", "2":"LED GREEN", "3":"LED RED", "4":"LED ORANGE", "5":"LED LIGHTERORANGE",
"6":"LED CORAL", "7":"LED YELLOW", "8":"LED LIGHTERGREEN", "9":"LED TURQUOISE", "0":"LED AQUA",
"BACK_LEFT":"LED NAVY", "OK":"LED WHITE"}

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
  global state, prev_state, alt_func, mapping
  with press_lock:
    if button_name == "SELECT":
      alt_func = True
      return
    if button_name in state_map:
      if state == state_map[button_name]:
        print("Help action requested -> do transition again")
        state = prev_state;
      match button_name:
        case "POWER":
          mapping = None
          if state in ("TV", "LIGHT"):
            threading.Thread(target=lambda: ir_send("LED POWER OFF")).start()
          if state in ("PROJECTOR", "DVD", "TV"):
            threading.Thread(target=lambda: ir_send("BAR POWER OFF")).start()
            if state in ("TV"):
              threading.Thread(target=lambda: ir_send("TV POWER OFF")).start()
            if state in ("PROJECTOR", "DVD"):
              threading.Thread(target=lambda: ir_send("BEAM POWER OFF")).start()
            if state in ("DVD"):
              threading.Thread(target=lambda: ir_send("DVD POWER OFF")).start()
        case "RED": # PROJECTOR -----
          mapping = map_PROJECTOR
          if state in ("TV", "LIGHT"):
            threading.Thread(target=lambda: ir_send("LED POWER OFF")).start()
          if state in ("TV", "DVD"):
            if state in ("TV"):
              threading.Thread(target=lambda: ir_send("TV POWER OFF")).start()
            if state in ("DVD"):
              threading.Thread(target=lambda: ir_send("DVD POWER OFF")).start()
          else:
            threading.Thread(target=lambda: ir_send("BAR POWER ON")).start()
          if not state in ("DVD"):
            threading.Thread(target=lambda: ir_send("BEAM POWER ON")).start()
        case "GREEN": # TV -----
          mapping = map_TV
          if not state in ("LIGHT"):
            threading.Thread(target=lambda: ir_send("LED POWER ON")).start()
          if state in ("PROJECTOR", "DVD"):
            threading.Thread(target=lambda: ir_send("BEAM POWER OFF")).start()
            if state in ("DVD"):
              threading.Thread(target=lambda: ir_send("DVD POWER OFF")).start()
          else:
            threading.Thread(target=lambda: ir_send("BAR POWER ON")).start()
          threading.Thread(target=lambda: ir_send("TV POWER ON")).start()
        case "YELLOW": # LIGHT -----
          mapping = map_LIGHT
          if state in ("PROJECTOR", "DVD", "TV"):
            threading.Thread(target=lambda: ir_send("BAR POWER OFF")).start()
            if state in ("TV"):
              threading.Thread(target=lambda: ir_send("TV POWER OFF")).start()
            if state in ("PROJECTOR", "DVD"):
              threading.Thread(target=lambda: ir_send("BEAM POWER OFF")).start()
            if state in ("DVD"):
              threading.Thread(target=lambda: ir_send("DVD POWER OFF")).start()
          if not state in ("TV"):
            threading.Thread(target=lambda: ir_send("LED POWER ON")).start()
        case "BLUE": # DVD -----
          mapping = map_DVD
          if state in ("TV", "LIGHT"):
            threading.Thread(target=lambda: ir_send("LED POWER OFF")).start()
            if state in ("TV"):
              threading.Thread(target=lambda: ir_send("TV POWER OFF")).start()
          if not state in ("PROJECTOR", "TV"):
            threading.Thread(target=lambda: ir_send("BAR POWER ON")).start()
            if not state in ("PROJECTOR"):
              threading.Thread(target=lambda: ir_send("BEAM POWER ON")).start()
          threading.Thread(target=lambda: ir_send("DVD POWER ON")).start()
      prev_state = state
      state      = state_map[button_name]
    else:
      if mapping:
        threading.Thread(target=lambda: ir_send(f"{mapping.get(button_name, 'UNKNOWN')}")).start()
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

