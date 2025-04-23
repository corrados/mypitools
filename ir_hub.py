#!/usr/bin/env python3

# created with help of ChatGPT

import threading
import struct
import time
import evdev
import subprocess
import pigpio
import math
import spidev

out_pin     = 22
device_path = None
state       = "IDLE"
prev_state  = "IDLE"
mapping     = None
pi          = None
adb_shell   = None
alt_func    = True
toggle_bit  = 0
press_lock  = threading.Lock()
ir_lock     = threading.Lock()

state_map = {"1":"PROJECTOR", "2":"TV", "3":"LIGHT", "4":"DVD", "5":"TVFIRE", "POWER":"IDLE"}
state_rgb = {"PROJECTOR":[100, 0, 100], "TV":[0, 100, 0], "LIGHT":[0, 0, 100],
             "DVD":[100, 100, 0], "TVFIRE":[0, 100, 100], "IDLE":[0, 0, 0]}

# scancode to readable button name for Elgato EyeTV remote
scancode_offset = 4539649
scancode_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"CH+", 16:"GREEN", 17:"VOL-", 18:"OK",
19:"VOL+", 20:"YELLOW", 21:"CH-", 22:"BLUE", 23:"BACK_LEFT", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

# key mapping from EyeTV remote to other device remote
map_TV = {"CH+":"TV UP", "CH-":"TV DOWN", "VOL-":"TV LEFT", "VOL+":"TV RIGHT", "OK":"TV OK",
"1":"TV 1", "2":"TV 2", "3":"TV 3", "4":"TV 4", "5":"TV 5", "6":"TV 6", "7":"TV 7", "8":"TV 8",
"9":"TV 9", "0":"TV 0", "BACK_LEFT":"TV MENU", "MUTE":"BAR MUTE", "RED":"BAR VOL+", "YELLOW":"BAR VOL-",
"GREEN":"TV CH+", "BLUE":"TV CH-", "TEXT":"TV TEXT", "BACK_RIGHT":"TV LIST"}

map_DVD = {"CH+":"DVD UP", "CH-":"DVD DOWN", "VOL-":"DVD LEFT", "VOL+":"DVD RIGHT", "OK":"DVD OK",
"1":"DVD 1", "2":"DVD 2", "3":"DVD 3", "4":"DVD 4", "5":"DVD 5", "6":"DVD 6", "7":"DVD 7", "8":"DVD 8",
"9":"DVD 9", "0":"DVD 0", "BACK_LEFT":"DVD MENU", "RED":"BAR VOLUMEUP", "YELLOW":"BAR VOLUMEDOWN"}

map_PROJECTOR = {"RED":"BAR VOLUMEUP", "YELLOW":"BAR VOLUMEDOWN", "MUTE":"BAR MUTE"}

map_LIGHT = {"CH+":"LED BRIGHTER", "CH-":"LED DIMMER", "VOL-":"LED DIMMER", "VOL+":"LED BRIGHTER",
"RED":"LED BRIGHTER", "YELLOW":"LED DIMMER", "GREEN":"LED BRIGHTER", "BLUE":"LED DIMMER",
"1":"LED WHITE", "2":"LED ORANGE", "3":"LED ORANGE1", "4":"LED BLUE", "5":"LED BLUE1",
"6":"LED BLUE2", "7":"LED YELLOW", "8":"LED MAGENTA", "9":"LED MAGENTA1", "0":"LED MAGENTA2",
"BACK_LEFT":"LED RED1", "OK":"LED WHITE", "BACK_RIGHT":"LED SMOOTH"}

# TEST
#map_TV = {"CH+":"TVFIRE KEYCODE_DPAD_UP", "CH-":"TVFIRE KEYCODE_DPAD_DOWN",
#"VOL-":"TVFIRE KEYCODE_DPAD_LEFT", "VOL+":"TVFIRE KEYCODE_DPAD_RIGHT", "OK":"TVFIRE KEYCODE_DPAD_CENTER",
#"1":"BEAM POWER", "2":"BEAM SOURCE", "3":"BEAM MENU", "4":"BAR POWER", "5":"BAR BLUETOOTH",
#"6":"BAR OPTICAL", "7":"LED POWERON", "8":"LED POWEROFF", "9":"LED BRIGHTER", "0":"LED DIMMER",
#"BACK_LEFT":"TV MENU", "RED":"BAR PLUS", "YELLOW":"BAR MINUS",
#"GREEN":"TV CHANNELUP", "BLUE":"TV CHANNELDOWN",
#"PLAY":"BAR PLAY", "STOP":"BAR STOP", "REWIND":"BAR BACK", "FORWARD":"BAR NEXT"}


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
      set_rgb([255, 0, 0]) # RED
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
            ir_send_in_thread("BAR POWER")
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
    set_rgb(state_rgb[state]) # always update RGB LED

def switch_tv_on(cur_state, input):
  ir_send_in_thread("TV POWERON") # immediate attempt
  ir_send_in_thread(f"TV {input}")
  time.sleep(10) # after cold start, it takes long until it starts
  if state in (cur_state): # only continue if still in TV state
    ir_send_in_thread("TV POWERON") # try again after a while
    time.sleep(5)
    if state in (cur_state): # only continue if still in TV state
      ir_send_in_thread(f"TV {input}")

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
      if device == "TVFIRE":
        send_keyevent(command)
      else:
        # introduce repeating of commands to make sure not command will get lost if, e.g.,
        # a person is in between IR transmitter and receiver
        repeat = 3
        send_command(device, command, repeat)

def adb_connect(ip_address): # returns True on success
  try:
    r = subprocess.run(["adb", "connect", f"{ip_address}:5555"], capture_output=True, text=True)
    if "connected" in r.stdout or "already connected" in r.stdout:
      start_adbshell()
      return True
    else:
      return False
  except Exception as e:
    return False

def start_adbshell():
  global adb_shell
  adb_shell = subprocess.Popen(
    ["adb", "shell"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

def send_keyevent(keycode):
  if adb_shell:
    adb_shell.stdin.write(f"input keyevent {keycode}\n")
    adb_shell.stdin.flush()

def set_rgb(rgb_leds):
  spi = spidev.SpiDev()
  spi.open(bus=0, device=0)
  spi.mode         = 0b00
  spi.max_speed_hz = 1000000
  spi.writebytes(rgb_leds)
  spi.close()

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

def carrier_frequency(out_pin, frequency, duty_cycle, duration, ir_signal):
  one_cycle_time = 1_000_000.0 / frequency
  on_duration    = round(one_cycle_time * duty_cycle)
  off_duration   = round(one_cycle_time * (1.0 - duty_cycle))
  total_cycles   = round(duration / one_cycle_time)
  for i in range(total_cycles):
    ir_signal.append(pigpio.pulse(1 << out_pin, 0, on_duration))
    ir_signal.append(pigpio.pulse(0, 1 << out_pin, off_duration))

def ir_sling(out_pin, frequency, duty_cycle, leading_pulse_duration, leading_gap_duration,
             one_pulse, zero_pulse, one_gap, zero_gap, send_trailing_pulse, trailing_gap, code, repeat, rc6_mode):
  # generate waveform
  ir_signal = []
  if rc6_mode:
    T = 444 # base unit
    carrier_frequency(out_pin, frequency, duty_cycle, 6 * T, ir_signal) # RC6 header: 6T mark, 2T space
    ir_signal.append(pigpio.pulse(0, 0, 2 * T))
    carrier_frequency(out_pin, frequency, duty_cycle, T, ir_signal) # start bit (1T mark, 1T space), high then low
    ir_signal.append(pigpio.pulse(0, 0, T))
    for i in range(3): # the mode bits mb2 ... mb0: low then high
      ir_signal.append(pigpio.pulse(0, 0, T))
      carrier_frequency(out_pin, frequency, duty_cycle, T, ir_signal)
    if toggle_bit == 0: # header trailer bit is toggle bit (2T)
      ir_signal.append(pigpio.pulse(0, 0, 2 * T))
      carrier_frequency(out_pin, frequency, duty_cycle, 2 * T, ir_signal)
    else:
      carrier_frequency(out_pin, frequency, duty_cycle, 2 * T, ir_signal)
      ir_signal.append(pigpio.pulse(0, 0, 2 * T))
    # encode all bits using Manchester, where each bit is 2T
    for i, bit in enumerate(code):
      if bit == '0': # high then low
        carrier_frequency(out_pin, frequency, duty_cycle, T, ir_signal)
        ir_signal.append(pigpio.pulse(0, 0, T))
      else:          # low then high
        ir_signal.append(pigpio.pulse(0, 0, T))
        carrier_frequency(out_pin, frequency, duty_cycle, T, ir_signal)
    ir_signal.append(pigpio.pulse(0, 0, 6 * T)) # signal free time is set to 6T, which is 2.666 ms
  else:
    carrier_frequency(out_pin, frequency, duty_cycle, leading_pulse_duration, ir_signal)
    ir_signal.append(pigpio.pulse(0, 0, leading_gap_duration))
    for char in code:
      if char == '0':
        carrier_frequency(out_pin, frequency, duty_cycle, zero_pulse, ir_signal)
        ir_signal.append(pigpio.pulse(0, 0, zero_gap))
      elif char == '1':
        carrier_frequency(out_pin, frequency, duty_cycle, one_pulse, ir_signal)
        ir_signal.append(pigpio.pulse(0, 0, one_gap))
    if send_trailing_pulse:
      carrier_frequency(out_pin, frequency, duty_cycle, one_pulse, ir_signal)
    if trailing_gap:
      ir_signal.append(pigpio.pulse(0, 0, trailing_gap))
  # transmit waveform
  pi.wave_clear()
  pi.wave_add_generic(ir_signal)
  wave_id = pi.wave_create()
  if wave_id >= 0:
    for i in range(repeat):
      pi.wave_send_once(wave_id)
      while pi.wave_tx_busy():
        time.sleep(0.01)
    pi.wave_delete(wave_id)
  return 0

def send_command(device, command, repeat=1):
    global toggle_bit
    # Lirc .conf format:
    #  header       9079  4405    -> leadingPulseDuration, leadingGapDuration
    #  one           638  1612    -> onePulse,             oneGap
    #  zero          638   473    -> zeroPulse,            zeroGap
    #  ptrail        642          -> sendTrailingPulse
    #  repeat       9070  2159
    #  gap          107799
    #  toggle_bit      0
    (leading_pulse_duration, leading_gap_duration) = (0, 0)
    (one_pulse,              zero_pulse)           = (0, 0)
    (one_gap,                zero_gap)             = (0, 0)
    frequency           = 38000
    duty_cycle          = 0.5
    rc6_mode            = False # default: no RC6
    send_trailing_pulse = 1     # default: send trailing pulse
    trailing_gap        = 0
    curkey              = []

    if device == "BAR": # Philips soundbar HTL2163B
      # bits           21
      # flags RC6|CONST_LENGTH
      # eps            30
      # aeps          100
      # gap          107636
      # toggle_bit_mask 0x10000
      # rc6_mask    0x10000
      frequency   = 36000
      rc6_mode    = True
      toggle_bit ^= 1

      bar_keys = {
        "POWER":       "1110111111110011", # 0x0EEFF3
        "COAX":        "1110111111000110", # 0x0EEFC6
        "OPTICAL":     "1110111110010011", # 0x0EEF93
        "AUX":         "1110111111000111", # 0x0EEFC7
        "AUDIOIN":     "1110111101111001", # 0x0EEF79
        "USB":         "1110111110000001", # 0x0EEF81
        "BLUETOOTH":   "1110111110010110", # 0x0EEF96
        "HDMIARC":     "1110111101111000", # 0x0EEF78
        "PREV":        "1110111110100101", # 0x0EEFA5
        "PLAY":        "1110111111010011", # 0x0EEFD3
        "NEXT":        "1110111110100100", # 0x0EEFA4
        "VOL+":        "1110111111101111", # 0x0EEFEF
        "VOL-":        "1110111111101110", # 0x0EEFEE
        "MUTE":        "1110111111110010", # 0x0EEFF2
        "TREB+":       "1110111111100111", # 0x0EEFE7
        "TREB-":       "1110111111100110", # 0x0EEFE6
        "BASS+":       "1110111111101001", # 0x0EEFE9
        "BASS-":       "1110111111101000", # 0x0EEFE8
        "SOUND":       "1110111110101110", # 0x0EEFAE
        "SURROUNDON":  "1110111110101101", # 0x0EEFAD
        "SURROUNDOFF": "1110111110101111", # 0x0EEFAF
        "SYNC+":       "1110111100000100", # 0x0EEF04
        "SYNC-":       "1110111100000101", # 0x0EEF05
        "DIM":         "1110111100010110", # 0x0EEF16
        "NIGHT":       "1110111100100011", # 0x0EEF23
      }
      curkey = bar_keys.get(command, [])

    elif device == "BEAM": # Ultimea P20 projector
      # bits           32
      # flags SPACE_ENC|CONST_LENGTH
      # eps            30
      # aeps          100
      # header       9044  4428
      # one           619  1629
      # zero          619   507
      # ptrail        621
      # repeat       9017  2205
      # gap          107794
      # toggle_bit_mask 0x0
      # frequency    38000
      leading_pulse_duration = 9044
      leading_gap_duration   = 4428
      one_pulse              = 619
      zero_pulse             = 619
      one_gap                = 1629
      zero_gap               = 507

      beam_keys = {
        "POWER":  "00011101111010100011000011001111", # 0x1DEA30CF 0xFFFFFFFF
        "MUTE":   "00011101111010101011000001001111", # 0x1DEAB04F 0xFFFFFFFF
        "REW":    "00011101111010100011100011000111", # 0x1DEA38C7 0xFFFFFFFF
        "PLAY":   "00011101111010100001100011100111", # 0x1DEA18E7 0xFFFFFFFF
        "FORW":   "00011101111010101011100001000111", # 0x1DEAB847 0xFFFFFFFF
        "UP":     "00011101111010100000100011110111", # 0x1DEA08F7 0xFFFFFFFF
        "LEFT":   "00011101111010101000100001110111", # 0x1DEA8877 0xFFFFFFFF
        "OK":     "00011101111010100100100010110111", # 0x1DEA48B7 0xFFFFFFFF
        "RIGHT":  "00011101111010101100100000110111", # 0x1DEAC837 0xFFFFFFFF
        "DOWN":   "00011101111010100010100011010111", # 0x1DEA28D7 0xFFFFFFFF
        "SOURCE": "00011101111010100111000010001111", # 0x1DEA708F 0xFFFFFFFF
        "MENU":   "00011101111010100110100010010111", # 0x1DEA6897 0xFFFFFFFF
        "EXIT":   "00011101111010101110100000010111", # 0x1DEAE817 0xFFFFFFFF
        "VOL-":   "00011101111010100111100010000111", # 0x1DEA7887 0xFFFFFFFF
        "VOL+":   "00011101111010101111100000000111", # 0x1DEAF807 0xFFFFFFFF
      }
      curkey = beam_keys.get(command, [])

    elif device == "LED": # Osram LED stribe
      # flags SPACE_ENC|CONST_LENGTH
      # eps            30
      # aeps          100
      # header       9067  4439
      # one           604  1645
      # zero          604   519
      # ptrail        604
      # repeat       9066  2198
      # gap          108051
      # toggle_bit_mask 0x0
      # frequency    38000
      leading_pulse_duration = 9067
      leading_gap_duration   = 4439
      one_pulse              = 604
      zero_pulse             = 604
      one_gap                = 1645
      zero_gap               = 519

      led_keys = {
        "POWEROFF": "10000000011111110110000110011110", # 0x807F619E 0xFFFFFFFF
        "POWERON":  "10000000011111111110000100011110", # 0x807FE11E 0xFFFFFFFF
        "BRIGHTER": "10000000011111111010000101011110", # 0x807FA15E 0xFFFFFFFF
        "DIMMER":   "10000000011111110010000111011110", # 0x807F21DE 0xFFFFFFFF
        "RED":      "10000000011111111001000101101110", # 0x807F916E 0xFFFFFFFF
        "GREEN":    "10000000011111110001000111101110", # 0x807F11EE 0xFFFFFFFF
        "BLUE":     "10000000011111110101000110101110", # 0x807F51AE 0xFFFFFFFF
        "WHITE":    "10000000011111111101000100101110", # 0x807FD12E 0xFFFFFFFF
        "FLASH":    "10000000011111111111000100001110", # 0x807FF10E 0xFFFFFFFF
        "STROBE":   "10000000011111111110100100010110", # 0x807FE916 0xFFFFFFFF
        "FADE":     "10000000011111111101100100100110", # 0x807FD926 0xFFFFFFFF
        "SMOOTH":   "10000000011111111100100100110110", # 0x807FC936 0xFFFFFFFF
        "RED1":     "10000000011111111011000101001110", # 0x807FB14E 0xFFFFFFFF
        "ORANGE":   "10000000011111111010100101010110", # 0x807FA956 0xFFFFFFFF
        "ORANGE1":  "10000000011111111001100101100110", # 0x807F9966 0xFFFFFFFF
        "YELLOW":   "10000000011111111000100101110110", # 0x807F8976 0xFFFFFFFF
        "GREEN1":   "10000000011111110011000111001110", # 0x807F31CE 0xFFFFFFFF
        "CYAN":     "10000000011111110010100111010110", # 0x807F29D6 0xFFFFFFFF
        "GREY":     "10000000011111110001100111100110", # 0x807F19E6 0xFFFFFFFF
        "BLUE1":    "10000000011111110111000110001110", # 0x807F718E 0xFFFFFFFF
        "BLUE2":    "10000000011111110000100111110110", # 0x807F09F6 0xFFFFFFFF
        "MAGENTA":  "10000000011111110110100110010110", # 0x807F6996 0xFFFFFFFF
        "MAGENTA1": "10000000011111110101100110100110", # 0x807F59A6 0xFFFFFFFF
        "MAGENTA2": "10000000011111110100100110110110", # 0x807F49B6 0xFFFFFFFF
      }
      curkey = led_keys.get(command, [])

    elif device == "TV": # Toshiba 42XV635D
      # bits           32
      # flags SPACE_ENC|CONST_LENGTH
      # eps            30
      # aeps          100
      # header       9018  4440
      # one           619  1621
      # zero          619   507
      # ptrail        620
      # repeat       9021  2185
      # gap          107862
      # toggle_bit_mask 0x0
      # frequency    38000
      leading_pulse_duration = 9000
      leading_gap_duration   = 4500
      one_pulse              = 619
      zero_pulse             = 619
      one_gap                = 1621
      zero_gap               = 507

      tv_keys = {
        "OK":          "00000010111111011000010001111011", # 0x02FD847B 0x00000000
        "UP":          "00000010111111011001100001100111", # 0x02FD9867 0x00000000
        "DOWN":        "00000010111111011011100001000111", # 0x02FDB847 0x00000000
        "RIGHT":       "00000010111111010000001011111101", # 0x02FD02FD 0x00000000
        "LEFT":        "00000010111111010100001010111101", # 0x02FD42BD 0x00000000
        "CH+":         "00000010111111011101100000100111", # 0x02FDD827 0x00000000
        "CH-":         "00000010111111011111100000000111", # 0x02FDF807 0x00000000
        "VOL+":        "00000010111111010101100010100111", # 0x02FD58A7 0x00000000
        "VOL-":        "00000010111111010111100010000111", # 0x02FD7887 0x00000000
        "MUTE":        "00000010111111010000100011110111", # 0x02FD08F7 0x00000000
        "1":           "00000010111111011000000001111111", # 0x02FD807F 0x00000000
        "2":           "00000010111111010100000010111111", # 0x02FD40BF 0x00000000
        "3":           "00000010111111011100000000111111", # 0x02FDC03F 0x00000000
        "4":           "00000010111111010010000011011111", # 0x02FD20DF 0x00000000
        "5":           "00000010111111011010000001011111", # 0x02FDA05F 0x00000000
        "6":           "00000010111111010110000010011111", # 0x02FD609F 0x00000000
        "7":           "00000010111111011110000000011111", # 0x02FDE01F 0x00000000
        "8":           "00000010111111010001000011101111", # 0x02FD10EF 0x00000000
        "9":           "00000010111111011001000001101111", # 0x02FD906F 0x00000000
        "0":           "00000010111111010000000011111111", # 0x02FD00FF 0x00000000
        "AD":          "00000010111111010100011010111001", # 0x02FD46B9 0x00000000
        "EXIT":        "00000010111111011100001000111101", # 0x02FDC23D 0x00000000
        "TEXT":        "00000010111111011110100000010111", # 0x02FDE817 0x00000000
        "SOURCE":      "00000010111111010010100011010111", # 0x02FD28D7 0x00000000
        "EXT1":        "00000010111111011000110001110011",
        "EXT2":        "00000010111111010100110010110011",
        "EXT3":        "00000010111111011100110000110011",
        "HDMI1":       "00000010111111010001110011100011",
        "HDMI2":       "00000010111111011001110001100011",
        "HDMI3":       "00000010111111010101110010100011",
        "HDMI4":       "00000010111111011101110000100011",
        "QUICK":       "00000010111111011100011000111001", # 0x02FDC639 0x00000000
        "GUIDE":       "00000010111111011010001001011101", # 0x02FDA25D 0x00000000
        "INFO":        "00000010111111010110100010010111", # 0x02FD6897 0x00000000
        "RETURN":      "00000010111111010010011011011001", # 0x02FD26D9 0x00000000
        "RED":         "00000010111111010001001011101101", # 0x02FD12ED 0x00000000
        "GREEN":       "00000010111111011001001001101101", # 0x02FD926D 0x00000000
        "YELLOW":      "00000010111111010101001010101101", # 0x02FD52AD 0x00000000
        "BLUE":        "00000010111111011101001000101101", # 0x02FDD22D 0x00000000
        "STEREO":      "00000010111111011100100000110111", # 0x02FDC837 0x00000000
        "SUBTITLE":    "00000010111111010011000011001111", # 0x02FD30CF 0x00000000
        "ASPECTRATIO": "00000010111111011001101001100101", # 0x02FD9A65 0x00000000
        "MENU":        "00000010111111011101101000100101", # 0x02FDDA25 0x00000000
        "TV":          "00000010111111010010001011011101", # 0x02FD22DD 0x00000000
        "LIST":        "00000010111111011110001000011101", # 0x02FDE21D 0x00000000
        "PAUSE":       "00000010111111010100010010111011", # 0x02FD44BB 0x00000000
        "PREVIOUS":    "00000010111111010010101011010101", # 0x02FD2AD5 0x00000000
        "REWIND":      "00000010111111011100101000110101", # 0x02FDCA35 0x00000000
        "FORWARD":     "00000010111111011011000001001111", # 0x02FDB04F 0x00000000
        "NEXT":        "00000010111111011010100001010111", # 0x02FDA857 0x00000000
        "POWER":       "00000010111111010100100010110111", # 0x02FD48B7 0x00000000
        "POWEROFF":    "00000010111111011111111000000001", # 0x02FDFE01 0x00000000
        "POWERON":     "00000010111111010111111010000001", # 0x02FD7E81 0x00000000
      }
      curkey = tv_keys.get(command, [])

    elif device == "DVD":
      # bits           12
      # flags SPACE_ENC|CONST_LENGTH
      # eps            30
      # aeps          100
      # header       2448   552
      # one          1240   557
      # zero          640   557
      # post_data_bits  8
      # post_data      0x47
      # gap          45087   ->   14400?
      # toggle_bit_mask 0x0
      # frequency    38000
      leading_pulse_duration = 2448
      leading_gap_duration   = 552
      one_pulse              = 1240
      zero_pulse             = 640
      one_gap                = 557
      zero_gap               = 557
      send_trailing_pulse    = 0
      trailing_gap           = 14400

      dvd_keys = {
        "POWER":     "10101000101101000111", # 0xA8B 0x47
        "OK":        "10111100101101000111", # 0xBCB 0x47
        "UP":        "10011100101101000111", # 0x9CB 0x47
        "DOWN":      "01011100101101000111", # 0x5CB 0x47
        "RIGHT":     "00111100101101000111", # 0x3CB 0x47
        "LEFT":      "11011100101101000111", # 0xDCB 0x47
        "AUDIO":     "00100110101101000111", # 0x26B 0x47
        "SUBTITLE":  "11000110101101000111", # 0xC6B 0x47
        "1":         "00000000101101000111", # 0x00B 0x47
        "2":         "10000000101101000111", # 0x80B 0x47
        "3":         "01000000101101000111", # 0x40B 0x47
        "4":         "11000000101101000111", # 0xC0B 0x47
        "5":         "00100000101101000111", # 0x20B 0x47
        "6":         "10100000101101000111", # 0xA0B 0x47
        "7":         "01100000101101000111", # 0x60B 0x47
        "8":         "11100000101101000111", # 0xE0B 0x47
        "9":         "00010000101101000111", # 0x10B 0x47
        "0":         "10010000101101000111", # 0x90B 0x47
        "RETURN":    "11000010101101000111", # 0xC2B 0x47
        "PLAY":      "01011000101101000111", # 0x58B 0x47
        "PAUSE":     "10011000101101000111", # 0x98B 0x47
        "STOP":      "00011000101101000111", # 0x18B 0x47
        "HOME":      "01000010101101000111", # 0x42B 0x47
        "POPUPMENU": "10010100101101000111", # 0x94B 0x47
        "EJECT":     "01101000101101000111", # 0x68B 0x47
        "RED":       "11100110101101000111", # 0xE6B 0x47
        "GREEN":     "00010110101101000111", # 0x16B 0x47
        "YELLOW":    "10010110101101000111", # 0x96B 0x47
        "BLUE":      "01100110101101000111", # 0x66B 0x47
        "TOPMENU":   "00110100101101000111", # 0x34B 0x47
        "OPTIONS":   "11111100101101000111", # 0xFCB 0x47
        "PREVIOUS":  "11101010101101000111", # 0xEAB 0x47
        "NEXT":      "01101010101101000111", # 0x6AB 0x47
        "REWIND":    "11011000101101000111", # 0xD8B 0x47
        "FORWARD":   "00111000101101000111", # 0x38B 0x47
        "DISPLAY":   "10000010101101000111", # 0x82B 0x47
        "SEN":       "00110010101101000111", # 0x32B 0x47
        "HEART":     "01111010101101000111", # 0x7AB 0x47
      }
      curkey = dvd_keys.get(command, [])

    if curkey:
      ir_sling(out_pin, frequency, duty_cycle,
               leading_pulse_duration, leading_gap_duration,
               one_pulse, zero_pulse, one_gap, zero_gap,
               send_trailing_pulse, trailing_gap, curkey, repeat, rc6_mode)

if __name__ == '__main__':
  target_device = None
  for device in [evdev.InputDevice(path) for path in evdev.list_devices()]:
    if "EyeTV" in device.name:
      target_device = device
  if target_device:
    adb_connect('firetv')
    start_pigpiod()
    device_path = target_device.path
    threading.Thread(target=watch_input).start()
  else:
    raise RuntimeError(f"Input device EyeTV not found.")

