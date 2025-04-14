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
alt_func    = True
press_lock  = threading.Lock()
ir_lock     = threading.Lock()

state_map = {"1":"PROJECTOR", "2":"TV", "3":"LIGHT", "4":"DVD", "POWER":"IDLE"}

# scancode to readable button name for Elgato EyeTV remote
scancode_offset = 4539649
scancode_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"CH+", 16:"GREEN", 17:"VOL-", 18:"OK",
19:"VOL+", 20:"YELLOW", 21:"CH-", 22:"BLUE", 23:"BACK_LEFT", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

# key mapping from EyeTV remote to other device remote
map_TV = {"CH+":"TV UP", "CH-":"TV DOWN", "VOL-":"TV LEFT", "VOL+":"TV RIGHT", "OK":"TV OK",
"1":"TV 1", "2":"TV 2", "3":"TV 3", "4":"TV 4", "5":"TV 5", "6":"TV 6", "7":"TV 7", "8":"TV 8",
"9":"TV 9", "0":"TV 0", "BACK_LEFT":"TV MENU", "RED":"BAR VOLUMEUP", "YELLOW":"BAR VOLUMEDOWN",
"GREEN":"TV CHANNELUP", "BLUE":"TV CHANNELDOWN"}
map_DVD = {"CH+":"DVD UP", "CH-":"DVD DOWN", "VOL-":"DVD LEFT", "VOL+":"DVD RIGHT", "OK":"DVD OK",
"1":"DVD 1", "2":"DVD 2", "3":"DVD 3", "4":"DVD 4", "5":"DVD 5", "6":"DVD 6", "7":"DVD 7", "8":"DVD 8",
"9":"DVD 9", "0":"DVD 0", "BACK_LEFT":"DVD MENU", "RED":"BAR VOLUMEUP", "YELLOW":"BAR VOLUMEDOWN"}
map_PROJECTOR = {"RED":"BAR VOLUMEUP", "YELLOW":"BAR VOLUMEDOWN", "MUTE":"BAR MUTE"}
map_LIGHT = {"CH+":"LED BRIGHTER", "CH-":"LED DIMMER", "VOL-":"LED DIMMER", "VOL+":"LED BRIGHTER",
"RED":"LED BRIGHTER", "YELLOW":"LED DIMMER", "GREEN":"LED BRIGHTER", "BLUE":"LED DIMMER",
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
          if scancode != last_scancode or time.time() - last_time > 0.2 or button_name in {"RED", "YELLOW"}:
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
    if (alt_func or button_name == "POWER") and button_name in state_map:
      if state == state_map[button_name]:
        print("Help action requested -> do transition again")
        state = prev_state;
      match button_name:
        case "POWER":
          mapping  = None
          alt_func = True # per definition, to be able to select mode right away
          if state in ("TV", "LIGHT"):
            ir_send_in_thread("LED POWER OFF")
          if state in ("PROJECTOR", "DVD", "TV"):
            ir_send_in_thread("BAR POWER OFF")
            if state in ("TV"):
              ir_send_in_thread("TV POWER OFF")
            if state in ("PROJECTOR", "DVD"):
              ir_send_in_thread("BEAM POWER OFF")
            if state in ("DVD"):
              ir_send_in_thread("DVD POWER OFF")
        case "1": # PROJECTOR -----
          mapping  = map_PROJECTOR
          alt_func = False
          if state in ("TV", "LIGHT"):
            ir_send_in_thread("LED POWER OFF")
          if state in ("TV", "DVD"):
            if state in ("TV"):
              ir_send_in_thread("TV POWER OFF")
            if state in ("DVD"):
              ir_send_in_thread("DVD POWER OFF")
          else:
            ir_send_in_thread("BAR BLUETOOTH") # powers it on, too
          if not state in ("DVD"):
            threading.Thread(target=switch_projector_on_with_input_select, args=("HDMI2",)).start()
          else:
            ir_send_in_thread("BEAM HDMI2")
        case "2": # TV -----
          mapping  = map_TV
          alt_func = False
          if not state in ("LIGHT"):
            ir_send_in_thread("LED POWER ON")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWER OFF")
            if state in ("DVD"):
              ir_send_in_thread("DVD POWER OFF")
          else:
            ir_send_in_thread("BAR OPTICAL") # powers it on, too
          threading.Thread(target=switch_tv_on).start() # special function needed for TV ON
        case "3": # LIGHT -----
          mapping  = map_LIGHT
          alt_func = False
          if state in ("PROJECTOR", "DVD", "TV"):
            ir_send_in_thread("BAR POWER OFF")
            if state in ("TV"):
              ir_send_in_thread("TV POWER OFF")
            if state in ("PROJECTOR", "DVD"):
              ir_send_in_thread("BEAM POWER OFF")
            if state in ("DVD"):
              ir_send_in_thread("DVD POWER OFF")
          if not state in ("TV"):
            ir_send_in_thread("LED POWER ON")
        case "4": # DVD -----
          mapping  = map_DVD
          alt_func = False
          if state in ("TV", "LIGHT"):
            ir_send_in_thread("LED POWER OFF")
            if state in ("TV"):
              ir_send_in_thread("TV POWER OFF")
          if not state in ("PROJECTOR", "TV"):
            ir_send_in_thread("BAR BLUETOOTH") # powers it on, too
          if not state in ("PROJECTOR"):
            threading.Thread(target=switch_projector_on_with_input_select, args=("HDMI1",)).start()
          else:
            ir_send_in_thread("BEAM HDMI1")
          ir_send_in_thread("DVD POWER ON")
      prev_state = state
      state      = state_map[button_name]
    else:
      if mapping:
        ir_send_in_thread(f"{mapping.get(button_name, 'UNKNOWN')}")
        alt_func = False # reset alternate function flag at the end of the function after send command

def switch_tv_on():
  ir_send_in_thread("TV POWER ON") # immediate attempt
  ir_send_in_thread("TV TV")
  time.sleep(10) # after cold start, it takes long until it starts
  if state == "TV": # only continue if still in TV state
    ir_send_in_thread("TV POWER ON") # try again after a while
    time.sleep(5)
    if state == "TV": # only continue if still in TV state
      ir_send_in_thread("TV TV")

def switch_projector_on_with_input_select(input):
  ir_send_in_thread("BEAM POWER ON")
  time.sleep(5)
  if state == "PROJECTOR": # only continue if still in PROJECTOR state
    ir_send_in_thread(f"BEAM {input}")

def ir_send_in_thread(send_arg):
  threading.Thread(target=ir_send, args=(send_arg,)).start()

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

