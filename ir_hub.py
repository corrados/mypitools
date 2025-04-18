#!/usr/bin/env python3

# created with help of ChatGPT

import threading
import struct
import time
import evdev
import subprocess
import pigpio
import math

MAX_COMMAND_SIZE = 512
MAX_PULSES       = 12000
device_path      = None
state            = "IDLE"
prev_state       = "IDLE"
mapping          = None
alt_func         = True
press_lock       = threading.Lock()
ir_lock          = threading.Lock()

state_map = {"1":"PROJECTOR", "2":"TV", "3":"LIGHT", "4":"DVD", "5":"TVFIRE", "POWER":"IDLE"}

# scancode to readable button name for Elgato EyeTV remote
scancode_offset = 4539649
scancode_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"CH+", 16:"GREEN", 17:"VOL-", 18:"OK",
19:"VOL+", 20:"YELLOW", 21:"CH-", 22:"BLUE", 23:"BACK_LEFT", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

# key mapping from EyeTV remote to other device remote
#map_TV = {"CH+":"TV UP", "CH-":"TV DOWN", "VOL-":"TV LEFT", "VOL+":"TV RIGHT", "OK":"TV OK",
#"1":"TV 1", "2":"TV 2", "3":"TV 3", "4":"TV 4", "5":"TV 5", "6":"TV 6", "7":"TV 7", "8":"TV 8",
#"9":"TV 9", "0":"TV 0", "BACK_LEFT":"TV MENU", "RED":"BAR VOLUMEUP", "YELLOW":"BAR VOLUMEDOWN",
#"GREEN":"TV CHANNELUP", "BLUE":"TV CHANNELDOWN"}

# TEST
map_TV = {"CH+":"TV UP", "CH-":"TV DOWN", "VOL-":"TV LEFT", "VOL+":"TV RIGHT", "OK":"TV OK",
"1":"BAR 1", "2":"BAR 2", "3":"BAR 3", "4":"BAR 4", "5":"BAR 5", "6":"BAR 6", "7":"BAR 7", "8":"BAR 8",
"9":"BAR 9", "0":"BAR 10", "BACK_LEFT":"TV MENU", "RED":"BAR PLUS", "YELLOW":"BAR MINUS",
"GREEN":"TV CHANNELUP", "BLUE":"TV CHANNELDOWN",
"PLAY":"BAR PLAY", "STOP":"BAR STOP", "REWIND":"BAR BACK", "FORWARD":"BAR NEXT"}


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
          if state in ("TV", "TVFIRE", "LIGHT"):
            ir_send_in_thread("LED POWER OFF")
          if state in ("PROJECTOR", "DVD", "TV", "TVFIRE"):
            ir_send_in_thread("BAR POWER OFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWER OFF")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWER OFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWER OFF")
        case "1": # PROJECTOR -----
          mapping  = map_PROJECTOR
          alt_func = False
          if state in ("TV", "TVFIRE", "LIGHT"):
            ir_send_in_thread("LED POWER OFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWER OFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWER OFF")
          ir_send_in_thread("BAR BLUETOOTH") # powers it on, too
          if not state in ("DVD"):
            threading.Thread(target=switch_projector_on_with_input_select, args=("PROJECTOR", "HDMI1",)).start()
          else:
            ir_send_in_thread("BEAM HDMI1")
        case "2" | "5": # TV/TVFIRE -----
          mapping  = map_TV
          alt_func = False
          if not state in ("LIGHT"):
            ir_send_in_thread("LED POWER ON")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWER OFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWER OFF")
          ir_send_in_thread("BAR OPTICAL") # powers it on, too
          if button_name == "2":
            if state in ("TVFIRE"):
              ir_send_in_thread(f"TV INPUTTV")
            else:
              threading.Thread(target=switch_tv_on, args=("TV", "TV",)).start()
          if button_name == "5":
            if state in ("TV"):
              ir_send_in_thread(f"TV INPUTHDMI1")
            else:
              threading.Thread(target=switch_tv_on, args=("TVFIRE", "HDMI1",)).start()
        case "3": # LIGHT -----
          mapping  = map_LIGHT
          alt_func = False
          if state in ("PROJECTOR", "DVD", "TV", "TVFIRE"):
            ir_send_in_thread("BAR POWER OFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWER OFF")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWER OFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWER OFF")
          if not state in ("TV", "TVFIRE"):
            ir_send_in_thread("LED POWER ON")
        case "4": # DVD -----
          mapping  = map_DVD
          alt_func = False
          if state in ("TV", "TVFIRE", "LIGHT"):
            ir_send_in_thread("LED POWER OFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWER OFF")
          ir_send_in_thread("BAR BLUETOOTH") # powers it on, too
          if not state in ("PROJECTOR"):
            threading.Thread(target=switch_projector_on_with_input_select, args=("DVD", "HDMI2",)).start()
          else:
            ir_send_in_thread("BEAM HDMI2")
          ir_send_in_thread("DVD POWER ON")
      prev_state = state
      state      = state_map[button_name]
    else:
      if mapping:
        ir_send_in_thread(f"{mapping.get(button_name, 'UNKNOWN')}")
        alt_func = False # reset alternate function flag at the end of the function after successful command send

def switch_tv_on(cur_state, input):
  ir_send_in_thread("TV POWER ON") # immediate attempt
  ir_send_in_thread(f"TV INPUT{input}")
  time.sleep(10) # after cold start, it takes long until it starts
  if state in (cur_state): # only continue if still in TV state
    ir_send_in_thread("TV POWER ON") # try again after a while
    time.sleep(5)
    if state in (cur_state): # only continue if still in TV state
      ir_send_in_thread(f"TV INPUT{input}")

def switch_projector_on_with_input_select(cur_state, input):
  ir_send_in_thread("BEAM POWER ON")
  time.sleep(5)
  if state in (cur_state): # only continue if still in PROJECTOR state
    ir_send_in_thread(f"BEAM {input}")

def ir_send_in_thread(send_arg):
  threading.Thread(target=ir_send, args=(send_arg,)).start()

def ir_send(button_name):
  with ir_lock:
    if not "UNKNOWN" in button_name:
      print(f"IR send {button_name}")
      subprocess.run([f"./ledremote {button_name}"], shell=True)
      subprocess.run([f"./ledremote {button_name}"], shell=True)



def add_pulse(on_pins, off_pins, duration, ir_signal):
    ir_signal.append(pigpio.pulse(on_pins, off_pins, duration))

def carrier_frequency(out_pin, frequency, duty_cycle, duration, ir_signal):
    one_cycle_time = 1_000_000.0 / frequency
    on_duration = round(one_cycle_time * duty_cycle)
    off_duration = round(one_cycle_time * (1.0 - duty_cycle))

    total_cycles = round(duration / one_cycle_time)
    for i in range(total_cycles):
        add_pulse(1 << out_pin, 0, on_duration, ir_signal)
        add_pulse(0, 1 << out_pin, off_duration, ir_signal)

def gap(duration, ir_signal):
    add_pulse(0, 0, int(duration), ir_signal)

def ir_sling(out_pin,
             frequency,
             duty_cycle,
             leading_pulse_duration,
             leading_gap_duration,
             one_pulse,
             zero_pulse,
             one_gap,
             zero_gap,
             send_trailing_pulse,
             code):

    if out_pin > 31:
        return 1  # Invalid pin

    if len(code) > MAX_COMMAND_SIZE:
        return 1  # Command too long

    ir_signal = []

    # Generate waveform
    carrier_frequency(out_pin, frequency, duty_cycle, leading_pulse_duration, ir_signal)
    gap(leading_gap_duration, ir_signal)

    for char in code:
        if char == '0':
            carrier_frequency(out_pin, frequency, duty_cycle, zero_pulse, ir_signal)
            gap(zero_gap, ir_signal)
        elif char == '1':
            carrier_frequency(out_pin, frequency, duty_cycle, one_pulse, ir_signal)
            gap(one_gap, ir_signal)
        else:
            print("Warning: Non-binary digit in command")

    if send_trailing_pulse:
        carrier_frequency(out_pin, frequency, duty_cycle, one_pulse, ir_signal)

    pi = pigpio.pi()
    if not pi.connected:
        print("GPIO Initialization failed")
        return 1

    pi.set_mode(out_pin, pigpio.OUTPUT)

    pi.wave_clear()
    pi.wave_add_generic(ir_signal)
    wave_id = pi.wave_create()

    if wave_id >= 0:
        pi.wave_send_once(wave_id)

        while pi.wave_tx_busy():
            time.sleep(0.01)

        pi.wave_delete(wave_id)

    pi.stop()
    return 0





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

