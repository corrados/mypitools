#!/usr/bin/env python3

# created with help of ChatGPT

import threading
import struct
import time
import evdev
import subprocess
import pigpio
import math

out_pin     = 22
device_path = None
state       = "IDLE"
prev_state  = "IDLE"
mapping     = None
pi          = None
adb_shell   = None
alt_func    = True
press_lock  = threading.Lock()
ir_lock     = threading.Lock()

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
            ir_send_in_thread("LED POWEROFF")
          if state in ("PROJECTOR", "DVD", "TV", "TVFIRE"):
            ir_send_in_thread("BAR POWEROFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWEROFF")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWEROFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWEROFF")
        case "1": # PROJECTOR -----
          mapping  = map_PROJECTOR
          alt_func = False
          if state in ("TV", "TVFIRE", "LIGHT"):
            ir_send_in_thread("LED POWEROFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWEROFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWEROFF")
          ir_send_in_thread("BAR BLUETOOTH") # powers it on, too
          if not state in ("DVD"):
            threading.Thread(target=switch_projector_on_with_input_select, args=("PROJECTOR", "HDMI1",)).start()
          else:
            ir_send_in_thread("BEAM HDMI1")
        case "2" | "5": # TV/TVFIRE -----
          mapping  = map_TV
          alt_func = False
          if not state in ("LIGHT"):
            ir_send_in_thread("LED POWERON")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWEROFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWEROFF")
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
            ir_send_in_thread("BAR POWEROFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWEROFF")
          if state in ("PROJECTOR", "DVD"):
            ir_send_in_thread("BEAM POWEROFF")
          if state in ("DVD"):
            ir_send_in_thread("DVD POWEROFF")
          if not state in ("TV", "TVFIRE"):
            ir_send_in_thread("LED POWERON")
        case "4": # DVD -----
          mapping  = map_DVD
          alt_func = False
          if state in ("TV", "TVFIRE", "LIGHT"):
            ir_send_in_thread("LED POWEROFF")
          if state in ("TV", "TVFIRE"):
            ir_send_in_thread("TV POWEROFF")
          ir_send_in_thread("BAR BLUETOOTH") # powers it on, too
          if not state in ("PROJECTOR"):
            threading.Thread(target=switch_projector_on_with_input_select, args=("DVD", "HDMI2",)).start()
          else:
            ir_send_in_thread("BEAM HDMI2")
          ir_send_in_thread("DVD POWERON")
      prev_state = state
      state      = state_map[button_name]
    else:
      if mapping:
        ir_send_in_thread(f"{mapping.get(button_name, 'UNKNOWN')}")
        alt_func = False # reset alternate function flag at the end of the function after successful command send

def switch_tv_on(cur_state, input):
  ir_send_in_thread("TV POWERON") # immediate attempt
  ir_send_in_thread(f"TV INPUT{input}")
  time.sleep(10) # after cold start, it takes long until it starts
  if state in (cur_state): # only continue if still in TV state
    ir_send_in_thread("TV POWERON") # try again after a while
    time.sleep(5)
    if state in (cur_state): # only continue if still in TV state
      ir_send_in_thread(f"TV INPUT{input}")

def switch_projector_on_with_input_select(cur_state, input):
  ir_send_in_thread("BEAM POWERON")
  time.sleep(5)
  if state in (cur_state): # only continue if still in PROJECTOR state
    ir_send_in_thread(f"BEAM {input}")

def ir_send_in_thread(send_arg):
  threading.Thread(target=ir_send, args=(send_arg,)).start()

def ir_send(button_name):
  with ir_lock:
    if not "UNKNOWN" in button_name:
      print(f"IR send {button_name}")
      device, command = button_name.strip().upper().split()
      send_command(device, command)
      send_command(device, command) # TEST send command twice
      #send_keyevent(20) # TEST ADB

def start_adbshell():
  global adb_shell
  adb_shell = subprocess.Popen(
      ["adb", "shell"],
      stdin=subprocess.PIPE,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      text=True)

def send_keyevent(keycode):
    adb_shell.stdin.write(f"input keyevent {keycode}\n")
    adb_shell.stdin.flush()

def start_pigpiod():
  global pi
  try:
    subprocess.run(["pidof", "pigpiod"], check=True, stdout=subprocess.DEVNULL)
  except subprocess.CalledProcessError:
    subprocess.Popen(["sudo", "pigpiod"])
    time.sleep(1)
  pi = pigpio.pi()
  if not pi.connected:
    print("GPIO Initialization failed")
    return 1
  pi.set_mode(out_pin, pigpio.OUTPUT)

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

def ir_sling(out_pin, frequency, duty_cycle, leading_pulse_duration, leading_gap_duration,
             one_pulse, zero_pulse, one_gap, zero_gap, send_trailing_pulse, code):
  # generate waveform
  ir_signal = []
  carrier_frequency(out_pin, frequency, duty_cycle, leading_pulse_duration, ir_signal)
  gap(leading_gap_duration, ir_signal)
  for char in code:
    if char == '0':
      carrier_frequency(out_pin, frequency, duty_cycle, zero_pulse, ir_signal)
      gap(zero_gap, ir_signal)
    elif char == '1':
      carrier_frequency(out_pin, frequency, duty_cycle, one_pulse, ir_signal)
      gap(one_gap, ir_signal)
  if send_trailing_pulse:
    carrier_frequency(out_pin, frequency, duty_cycle, one_pulse, ir_signal)
  # send waveform
  pi.wave_clear()
  pi.wave_add_generic(ir_signal)
  wave_id = pi.wave_create()
  if wave_id >= 0:
    pi.wave_send_once(wave_id)
    while pi.wave_tx_busy():
      time.sleep(0.01)
    pi.wave_delete(wave_id)
  return 0

def send_command(device, command):
    # Lirc .conf format:
    #  header       9079  4405    -> leadingPulseDuration, leadingGapDuration
    #  one           638  1612    -> onePulse,             oneGap
    #  zero          638   473    -> zeroPulse,            zeroGap
    #  ptrail        642          -> sendTrailingPulse
    #  repeat       9070  2159
    #  gap          107799
    #  toggle_bit      0
    frequency  = 38000
    duty_cycle = 0.5

    if device == "BAR":
      leading_pulse_duration = 2422
      leading_gap_duration   = 571
      one_pulse              = 1224
      zero_pulse             = 625
      one_gap                = 570
      zero_gap               = 570
      send_trailing_pulse = 1

      bar_keys = {
        "PLAY":  "010011010001", # 0x4D1
        "STOP":  "000111010001", # 0x1D1
        "MINUS": "110010010001", # 0xC91
        "PLUS":  "010010010001", # 0x491
        "BACK":  "000011010001", # 0x0D1
        "NEXT":  "100011010001", # 0x8D1
        "1":     "000000010001", # 0x011
        "2":     "100000010001", # 0x811
        "3":     "010000010001", # 0x411
        "4":     "110000010001", # 0xC11
        "5":     "001000010001", # 0x211
        "6":     "101000010001", # 0xA11
        "7":     "011000010001", # 0x611
        "8":     "111000010001", # 0xE11
        "9":     "000100010001", # 0x111
        "10":    "000001010001", # 0x051
      }
      curkey = bar_keys.get(command, [])

    elif device == "TV":
      leading_pulse_duration = 9000
      leading_gap_duration   = 4500
      one_pulse              = 528
      zero_pulse             = 528
      one_gap                = 1699
      zero_gap               = 599
      send_trailing_pulse    = 1

      tv_keys = {
        "POWER":      "0100100010110111",
        "OK":         "1000010001111011",
        "UP":         "1001100001100111",
        "DOWN":       "1011100001000111",
        "VOLUMEUP":   "0101100010100111",
        "VOLUMEDOWN": "0111100010000111",
        "1":          "1000000001111111",
        "2":          "0100000010111111",
        "3":          "1100000000111111",
        "4":          "0010000011011111",
        "5":          "1010000001011111",
        "6":          "0110000010011111",
        "7":          "1110000000011111",
        "8":          "0001000011101111",
        "9":          "1001000001101111",
        "0":          "0000000011111111",
        "MENU":       "1101101000100101",
        "BACK":       "0010011011011001",
        "EXIT":       "1100001000111101",
        "RIGHT":      "0000001011111101",
        "LEFT":       "0100001010111101",
        "TEXT":       "1110100000010111",
        "LIST":       "1110001000011101",
        "TV":         "0010001011011101",
      }
      curkey = tv_keys.get(command, [])

    elif device == "DVD":
      leading_pulse_duration = 2426
      leading_gap_duration   = 565
      one_pulse              = 1221
      zero_pulse             = 627
      one_gap                = 569
      zero_gap               = 569
      send_trailing_pulse    = 1

      dvd_keys = {
        "POWER":     "101010001011",
        "OK":        "101111001011",
        "UP":        "100111001011",
        "DOWN":      "010111001011",
        "AUDIO":     "001001101011",
        "SUBTITLE":  "110001101011",
        "1":         "000000001011",
        "2":         "100000001011",
        "3":         "010000001011",
        "4":         "110000001011",
        "5":         "001000001011",
        "6":         "101000001011",
        "7":         "011000001011",
        "8":         "111000001011",
        "9":         "000100001011",
        "0":         "100100001011",
        "MENU":      "001101001011",
        "RETURN":    "110000101011",
        "PLAY":      "010110001011",
        "RIGHT":     "001111001011",
        "LEFT":      "110111001011",
        "STOP":      "000110001011",
        "HOME":      "010000101011",
        "POPUPMENU": "100101001011",
      }
      curkey = dvd_keys.get(command, [])

    if device in ("BAR", "TV", "DVD") and curkey:
      return ir_sling(out_pin, frequency, duty_cycle,
                      leading_pulse_duration, leading_gap_duration,
                      one_pulse, zero_pulse, one_gap, zero_gap,
                      send_trailing_pulse, curkey)

if __name__ == '__main__':
  target_device = None
  for device in [evdev.InputDevice(path) for path in evdev.list_devices()]:
    if "EyeTV" in device.name:
      target_device = device
  if target_device:
    #start_adbshell() # TEST ADB
    start_pigpiod()
    device_path = target_device.path
    threading.Thread(target=watch_input).start()
  else:
    raise RuntimeError(f"Input device EyeTV not found.")

