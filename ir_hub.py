#!/usr/bin/env python3
# created with help of ChatGPT
import threading, struct, time, evdev, subprocess, pigpio, math, spidev, random, socket, os
from datetime import datetime

out_pin           = 22
ps3_bd_remote_mac = "00:1E:3D:10:CB:F0"
rgb_val           = 10 # 0..255
device_path       = None
state             = "IDLE"
prev_state        = "IDLE"
led_is_on         = False
mapping           = None
pi                = None
adb_shell         = None
ps3_sleep_timer   = None
alt_func          = True
toggle_bit        = 0
press_lock        = threading.Lock()
ir_lock           = threading.Lock()

state_map = {"1":"PROJECTOR", "2":"TV", "3":"LIGHT", "4":"DVD", "5":"TVFIRE", "POWER":"IDLE"}
state_rgb = {"PROJECTOR":[0, 0, rgb_val], "TV":[0, rgb_val, 0], "LIGHT":[rgb_val, rgb_val, rgb_val],
             "DVD":[rgb_val, rgb_val, 0], "TVFIRE":[rgb_val, 0, rgb_val], "IDLE":[0, 0, 0]}

# scancode to readable button name for Elgato EyeTV remote
eyetv_map = {0:"POWER", 1:"MUTE", 2:"1", 3:"2", 4:"3", 5:"4", 6:"5", 7:"6", 8:"7", 9:"8",
10:"9", 11:"LAST", 12:"0", 13:"ENTER", 14:"RED", 15:"UP", 16:"GREEN", 17:"LEFT", 18:"OK",
19:"RIGHT", 20:"YELLOW", 21:"DOWN", 22:"BLUE", 23:"RETURN", 24:"PLAY", 25:"BACK_RIGHT",
26:"REWIND", 27:"L", 28:"FORWARD", 29:"STOP", 30:"TEXT", 63:"REC", 64:"HOLD", 65:"SELECT"}

eyetv_convert = {"L":"LIGHT", "RED":"VOL+", "YELLOW":"VOL-", "GREEN":"CH+", "BLUE":"CH-"}

# scancode to readable button name for Playstation BD remote
playstation_map = {22:"EJECT", 0:"1", 1:"2", 2:"3", 3:"4", 4:"5", 5:"6", 6:"7", 7:"8", 8:"9", 9:"0",
100:"AUIDO", 101:"ANGLE", 99:"SUBTITLE", 15:"CLEAR", 40:"TIME", 129:"RED", 130:"GREEN", 128:"BLUE",
131:"YELLOW", 112:"DISPLAY", 26:"TOPMENU", 64:"POPUP", 14:"RETURN", 1048668:"OPTIONS", 2097245:"BACK",
268435540:"UP", 1073741910:"DOWN", 2147483735:"LEFT", 536870997:"RIGHT", 2059:"OK", 8388703:"VIEW",
4194398:"X", 262234:"L1", 65624:"L2", 33554513:"L3", 524379:"R1", 131161:"R2", 67108946:"R3",
323:"PLAYSTATION", 16777296:"SELECT", 134217811:"START", 51:"REWIND", 50:"PLAY", 52:"FORWARD",
48:"PREVIOUS", 56:"STOP", 49:"NEXT", 96:"SLOWREWIND", 57:"PAUSE", 97:"SLOWFORWARD"}

playstation_convert = {"EJECT":"POWER", "L1":"LIGHT", "AUIDO":"MUTE",
"SLOWREWIND":"HOLD", "SLOWFORWARD":"SELECT", "OPTIONS":"VOL+", "VIEW":"VOL-", "BACK":"CH+", "X":"CH-"}

# key mapping from EyeTV remote to other device remote
map_TV = {"UP":"TV UP", "DOWN":"TV DOWN", "LEFT":"TV LEFT", "RIGHT":"TV RIGHT", "OK":"TV OK",
"1":"TV 1", "2":"TV 2", "3":"TV 3", "4":"TV 4", "5":"TV 5", "6":"TV 6", "7":"TV 7", "8":"TV 8",
"9":"TV 9", "0":"TV 0", "RETURN":"TV RETURN", "LAST":"TV MENU", "MUTE":"BAR MUTE",
"VOL+":"BAR VOL+", "VOL-":"BAR VOL-", "CH+":"TV CH+", "CH-":"TV CH-",
"TEXT":"TV TEXT", "BACK_RIGHT":"TV LIST", "ENTER":"TV EXIT", "HOLD":"TV SOURCE"}

map_DVD = {"UP":"DVD UP", "DOWN":"DVD DOWN", "LEFT":"DVD LEFT", "RIGHT":"DVD RIGHT", "OK":"DVD OK",
"1":"DVD 1", "2":"DVD 2", "3":"DVD 3", "4":"DVD 4", "5":"DVD 5", "6":"DVD 6", "7":"DVD 7", "8":"DVD 8",
"9":"DVD 9", "0":"DVD 0", "LAST":"DVD MENU", "VOL+":"BAR VOL+", "VOL-":"BAR VOL-", "MUTE":"BAR MUTE",
"RETURN":"DVD RETURN", "ENTER":"DVD HOME", "TEXT":"DVD POPUPMENU", "BACK_RIGHT":"DVD OPTIONS",
"PLAY":"DVD PLAY", "FORWARD":"DVD FORWARD", "REWIND":"DVD REWIND", "STOP":"DVD STOP",
"REC":"DVD EJECT", "ANGLE":"DVD EJECT"}

# up:103,down:108,left:105,right:106,ok:96,back:158,menu:139,play:64,forward:208,rewind:168,apps:746 (input-event-codes.h)
map_PROJECTOR = {"VOL+":"BAR VOL+", "VOL-":"BAR VOL-", "MUTE":"BAR MUTE",
"HOLD":"BEAM SOURCE", "ENTER":"BEAM OK", "LAST":"BEAM EXIT",
"UP":"FIRETVBEAM 103", "DOWN":"FIRETVBEAM 108", "LEFT":"FIRETVBEAM 105", "RIGHT":"FIRETVBEAM 106",
"OK":"FIRETVBEAM 96", "RETURN":"FIRETVBEAM 158", "POPUP":"FIRETVBEAM 139", "REWIND":"FIRETVBEAM 168",
"PLAY":"FIRETVBEAM 164", "FORWARD":"FIRETVBEAM 208", "START":"FIRETVBEAM 746", "PLAYSTATION":"FIRETVBEAM 102"}

map_LIGHT = {"UP":"LED BRIGHTER", "DOWN":"LED DIMMER", "LEFT":"LED DIMMER", "RIGHT":"LED BRIGHTER",
"VOL+":"LED BRIGHTER", "VOL-":"LED DIMMER", "CH+":"LED BRIGHTER", "CH-":"LED DIMMER",
"RED":"LED RED",      "GREEN":"LED GREEN",  "YELLOW":"LED YELLOW", "BLUE":"LED BLUE",
"DISPLAY":"LED RED1", "TOPMENU":"LED CYAN", "POPUP":"LED ORANGE",  "RETURN":"LED MAGENTA"
"L2":"LED ORANGE1", "L3":"LED GREEN1", "R1":"LED GREY", "R2":"LED BLUE1", "R3":"LED MAGENTA1",
"OK":"LED WHITE", "PLAY":"LED SMOOTH"}

map_select = {"UP":"LED BRIGHTER", "DOWN":"LED DIMMER", "PLAY":"LED SMOOTH",
"VOL+":"BAR BASS+", "VOL-":"BAR BASS-", "CH+":"BAR TREB+", "CH-":"BAR TREB-",
"HOLD":"TV HDMI1", "STOP":"TV HDMI2", "TEXT":"TV HDMI3", "REC":"TV HDMI4",
"REWIND":"DVD AUDIO", "FORWARD":"DVD SUBTITLE",
"RED":"LED RED", "GREEN":"LED GREEN", "BLUE":"LED BLUE", "YELLOW":"LED YELLOW"}

def playstation_remote_input():
  global ps3_sleep_timer
  (HCI_ACLDATA_PKT, HCI_EVENT_PKT) = (0x02, 0x04)
  sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, 1)
  sock.bind((0,))
  sock.setsockopt(0, 2, struct.pack("LLLH", (1 << HCI_EVENT_PKT) | (1 << HCI_ACLDATA_PKT), 0xffffffff, 0xffffffff, 0))
  while True:
    pkt = sock.recv(2048)
    if pkt[0] == HCI_ACLDATA_PKT:
      if pkt[-7:-2] == bytes.fromhex("ffffffffff"):
        value = int.from_bytes(pkt[11:-7], byteorder="big")
        button_name = playstation_map.get(value, f"UNKNOWN ({value})")
        button_name = playstation_convert.get(button_name, button_name)
        threading.Thread(target=on_button_press, args=(button_name,)).start()
        # disconnect PS3 BD remote after 1.5h of inactivity to avoid battery drain (goes into sleep if disconnected)
        if ps3_sleep_timer is not None:
          ps3_sleep_timer.cancel()
        ps3_sleep_timer = threading.Timer(5400, lambda: subprocess.run(["bluetoothctl", "disconnect", ps3_bd_remote_mac]))
        ps3_sleep_timer.start()

def eyetv_remote_input():
  time.sleep(5) # let device be initialized in system on startup
  target_device = None
  for device in [evdev.InputDevice(path) for path in evdev.list_devices()]:
    if "EyeTV" in device.name:
      target_device = device
  if target_device:
    device_path = target_device.path
  else:
    raise RuntimeError("Input device EyeTV not found.")
  (last_scancode, last_time) = (None, 0) # state
  event_format = "qqHHI"
  event_size   = struct.calcsize(event_format)
  with open(device_path, "rb") as f:
    while True:
      data = f.read(event_size)
      if len(data) == event_size:
        _, _, event_type, code, value = struct.unpack(event_format, data)
        if event_type == 4 and code == 4: # MSC_SCAN
          scancode = value - 4539649
          button_name = eyetv_map.get(scancode, f"UNKNOWN ({scancode})")
          button_name = eyetv_convert.get(button_name, button_name)
          if scancode != last_scancode or time.time() - last_time > 0.2 or button_name in {"RED", "YELLOW"}:
            last_scancode = scancode
            threading.Thread(target=on_button_press, args=(button_name,)).start()
          last_time = time.time()
        elif event_type == 0: # EV_SYN (sync event)
          last_time = time.time() # mark last_time for idle detection

def socket_input():
  socket_path = "/tmp/ir_hub_script.sock"
  if os.path.exists(socket_path):
    os.remove(socket_path)
  server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  server_socket.bind(socket_path)
  server_socket.listen(1)
  while True:
    client_socket, _ = server_socket.accept()
    with client_socket:
      while True:
        data = client_socket.recv(1024)
        if not data:
          break
        on_button_press(data.decode('utf-8').strip())

def on_button_press(button_name):
  global state, prev_state, alt_func, mapping, led_is_on
  with press_lock:
    # special key: SELECT
    if button_name == "SELECT" and state != "IDLE":
      alt_func = not alt_func
    # special key: L
    if button_name == "LIGHT": # toggles LED state, do not change color or brightness per definition
      ir_send_in_thread("LED POWEROFF", 1) if led_is_on else ir_send_in_thread("LED POWERON", 1)
      led_is_on = not led_is_on
    # special key: POWER
    if (alt_func or button_name == "POWER") and button_name in state_map:
      alt_func = False # clear SELECT state
      if state == state_map[button_name]:
        print("Help action requested -> do transition again")
        state = prev_state;
      if button_name == "POWER":
        mapping   = None
        alt_func  = True # special case: per definition True, to be able to select mode right away
        led_is_on = False
        ir_send_in_thread("TV POWEROFF") # need to be first (avoid interference with IR signal)
        if state in ("TV", "TVFIRE"):
          time.sleep(3) # if TV was on, add delay (avoid interference with IR signal)
        if state in ("PROJECTOR", "DVD", "TV", "TVFIRE"):
          ir_send_in_thread("BAR POWER") # doesn't matter if incorrect, switches off after 15 minutes idle anyway
        if state in ("PROJECTOR", "DVD"):
          threading.Thread(target=switch_projector_off).start()
        ir_send_in_thread("DVD POWEROFF")
        ir_send_in_thread("LED POWEROFF")
      elif button_name == "1": # PROJECTOR -----
        mapping   = map_PROJECTOR
        led_is_on = False
        ir_send_in_thread("TV POWEROFF") # need to be first (avoid interference with IR signal)
        if state in ("TV", "TVFIRE"):
          time.sleep(3) # if TV was on, add delay (avoid interference with IR signal)
        threading.Thread(target=switch_bar_on, args=("BLUETOOTH",)).start()
        if not state in ("DVD"):
          threading.Thread(target=switch_projector_on_with_input_select, args=("PROJECTOR", "HDMI1",)).start()
        else:
          pass #ir_send_in_thread("BEAM HDMI1", 1)
        ir_send_in_thread("LED POWEROFF")
        ir_send_in_thread("DVD POWEROFF")
      elif button_name == "2" or button_name == "5": # TV/TVFIRE -----
        mapping   = map_TV
        led_is_on = True
        ir_send_in_thread("LED POWEROFF")
        if state in ("PROJECTOR", "DVD"):
          threading.Thread(target=switch_projector_off).start()
          time.sleep(3) # if TV was on, add delay (avoid interference with IR signal)
        ir_send_in_thread("DVD POWEROFF")
        if button_name == "2":
          if state in ("TVFIRE"):
            ir_send_in_thread("TV CH+", 1)
          else:
            threading.Thread(target=switch_tv_on, args=("TV", "CH+",)).start() # "TV TV" does not work correctly
        if button_name == "5":
          if state in ("TV"):
            ir_send_in_thread("TV HDMI1")
          else:
            threading.Thread(target=switch_tv_on, args=("TVFIRE", "HDMI1",)).start()
        threading.Thread(target=switch_bar_on, args=("OPTICAL",)).start()
      elif button_name == "3": # LIGHT -----
        mapping   = map_LIGHT
        led_is_on = True
        ir_send_in_thread("TV POWEROFF") # need to be first (avoid interference with IR signal)
        if state in ("TV", "TVFIRE"):
          time.sleep(3) # if TV was on, add delay (avoid interference with IR signal)
        if state in ("PROJECTOR", "DVD", "TV", "TVFIRE"):
          ir_send_in_thread("BAR POWER") # doesn't matter if incorrect, switches off after 15 minutes idle anyway
        ir_send_in_thread("LED POWERON")
        if state in ("PROJECTOR", "DVD"):
          threading.Thread(target=switch_projector_off).start()
        ir_send_in_thread("DVD POWEROFF")
        ir_send_in_thread("LED WHITE") # initial state of LED
        threading.Thread(target=led_max_brightness).start()
      elif button_name == "4": # DVD -----
        mapping   = map_DVD
        led_is_on = False
        ir_send_in_thread("TV POWEROFF") # need to be first (avoid interference with IR signal)
        if state in ("TV", "TVFIRE"):
          time.sleep(3) # if TV was on, add delay (avoid interference with IR signal)
        ir_send_in_thread("DVD POWERON")
        threading.Thread(target=switch_bar_on, args=("BLUETOOTH",)).start()
        if not state in ("PROJECTOR"):
          threading.Thread(target=switch_projector_on_with_input_select, args=("DVD", "HDMI2",)).start()
        else:
          pass #ir_send_in_thread("BEAM HDMI2", 1)
        ir_send_in_thread("LED POWEROFF")
      prev_state = state
      state      = state_map[button_name]
    else:
      if alt_func:
        ir_send_in_thread(f"{map_select.get(button_name, 'UNKNOWN')}", 1)
      elif mapping:
        ir_send_in_thread(f"{mapping.get(button_name, 'UNKNOWN')}", 1)
    if alt_func and state != "IDLE":
      set_rgb([255, 0, 0]) # RED at highest power
    else:
      set_rgb(state_rgb[state]) # always update RGB LED

def switch_tv_on(cur_state, input):
  ir_send_in_thread("TV POWERON") # immediate attempt
  time.sleep(5)
  if state in (cur_state): # only switch input if exactly the same state
    ir_send_in_thread(f"TV {input}", 1)
  time.sleep(10) # after cold start, it takes long until it starts
  if state in ("TV", "TVFIRE"): # only continue if still in TV state
    ir_send_in_thread("TV POWERON") # try again after a while
    # after cold start, do not switch input since it may be anoying after warm start

def switch_projector_on_with_input_select(cur_state, input):
  ir_send_in_thread("BEAM POWER")
  #time.sleep(5)
  #if state in (cur_state): # only continue if still in PROJECTOR state
  #  ir_send_in_thread(f"BEAM {input}", 1)

def switch_projector_off():
  for i in range(7):
    ir_send_in_thread("BEAM POWER", 1)
    time.sleep(0.1)

def switch_bar_on(input):
  ir_send_in_thread(f"BAR {input}") # powers it on, too
  time.sleep(15)
  ir_send_in_thread("BAR SURROUNDON") # we always want surround sound activated

def led_max_brightness():
  for i in range(10):
    ir_send_in_thread("LED BRIGHTER", 1)
    time.sleep(0.3)
  for i in range(3): # make it a bit dimmer since max brightness is too intense
    ir_send_in_thread("LED DIMMER", 1)
    time.sleep(0.3)

def ir_send_in_thread(button_name, repeat = 2):
  threading.Thread(target=ir_send, args=(button_name, repeat,)).start()

def ir_send(button_name, repeat):
  with ir_lock:
    if not "UNKNOWN" in button_name:
      print(f"IR send {button_name}")
      device, command = button_name.strip().upper().split()
      if device == "FIRETVBEAM":
        send_keyevent(command)
      else:
        send_command(device, command, repeat)

def adb_connect(ip_address): # returns True on success
  global adb_shell
  while True:
    try:
      r = subprocess.run(["adb", "connect", f"{ip_address}:5555"], capture_output=True, text=True)
      if "connected" in r.stdout or "already connected" in r.stdout:
        if not adb_shell or adb_shell.poll() is not None:
          terminate_adb_shell()
          adb_shell = subprocess.Popen(["adb", "shell"],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
      else:
        terminate_adb_shell()
    except Exception as e:
      terminate_adb_shell()
    time.sleep(5) # check adb status every 5 seconds

def terminate_adb_shell():
  global adb_shell
  if adb_shell:
    adb_shell.terminate()
  adb_shell = None

def send_keyevent(keycode):
  if adb_shell and adb_shell.poll() is None:
    adb_shell.stdin.write(f"sendevent /dev/input/event4 1 {keycode} 1 && sendevent /dev/input/event4 0 0 0 &&"
                          f"sendevent /dev/input/event4 1 {keycode} 0 && sendevent /dev/input/event4 0 0 0\n")
    adb_shell.stdin.flush()

def set_rgb(rgb_leds):
  spi = spidev.SpiDev()
  spi.open(bus=0, device=0)
  spi.mode         = 0b00
  spi.max_speed_hz = 1000000
  spi.writebytes(rgb_leds)
  spi.close()

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
  pi = pigpio.pi()
  if pi.connected:
    pi.set_mode(out_pin, pigpio.OUTPUT)
    pi.wave_clear()
    pi.wave_add_generic(ir_signal)
    wave_id = pi.wave_create()
    if wave_id >= 0:
      for i in range(repeat):
        pi.wave_send_once(wave_id)
        while pi.wave_tx_busy():
          time.sleep(0.01)
      pi.wave_delete(wave_id)
  pi.stop()

def send_command(device, command, repeat=1):
    global toggle_bit
    #  header       9079  4405    -> leadingPulseDuration, leadingGapDuration
    #  one           638  1612    -> onePulse,             oneGap
    #  zero          638   473    -> zeroPulse,            zeroGap
    #  ptrail        642          -> sendTrailingPulse
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
      # bits:21, flags:RC6|CONST_LENGTH, gap:107636, toggle_bit_mask:0x10000, rc6_mask:0x10000
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
      # bits:32, flags:SPACE_ENC|CONST_LENGTH, header:9044 4428
      # one:619 1629, zero:619 507, ptrail:621, repeat:9017 2205
      # gap: 107794, toggle_bit_mask:0x0
      leading_pulse_duration = 9044
      leading_gap_duration   = 4428
      one_pulse              = 619
      zero_pulse             = 619
      one_gap                = 1629
      zero_gap               = 507
      trailing_gap           = 10000
      beam_keys = {
        "POWER":  "00011101111010100011000011001111", # 0x1DEA30CF
        "MUTE":   "00011101111010101011000001001111", # 0x1DEAB04F
        "REW":    "00011101111010100011100011000111", # 0x1DEA38C7
        "PLAY":   "00011101111010100001100011100111", # 0x1DEA18E7
        "FORW":   "00011101111010101011100001000111", # 0x1DEAB847
        "UP":     "00011101111010100000100011110111", # 0x1DEA08F7
        "LEFT":   "00011101111010101000100001110111", # 0x1DEA8877
        "OK":     "00011101111010100100100010110111", # 0x1DEA48B7
        "RIGHT":  "00011101111010101100100000110111", # 0x1DEAC837
        "DOWN":   "00011101111010100010100011010111", # 0x1DEA28D7
        "SOURCE": "00011101111010100111000010001111", # 0x1DEA708F
        "MENU":   "00011101111010100110100010010111", # 0x1DEA6897
        "EXIT":   "00011101111010101110100000010111", # 0x1DEAE817
        "VOL-":   "00011101111010100111100010000111", # 0x1DEA7887
        "VOL+":   "00011101111010101111100000000111", # 0x1DEAF807
      }
      curkey = beam_keys.get(command, [])

    elif device == "LED": # Osram LED stribe
      # flags:SPACE_ENC|CONST_LENGTH, header:9067 4439
      # one:604 1645, zero:604 519, ptrail: 604, repeat:9066 2198
      # gap: 108051, toggle_bit_mask:0x0
      leading_pulse_duration = 9067
      leading_gap_duration   = 4439
      one_pulse              = 604
      zero_pulse             = 604
      one_gap                = 1645
      zero_gap               = 519
      trailing_gap           = 110000
      led_keys = {
        "POWEROFF": "10000000011111110110000110011110", # 0x807F619E
        "POWERON":  "10000000011111111110000100011110", # 0x807FE11E
        "BRIGHTER": "10000000011111111010000101011110", # 0x807FA15E
        "DIMMER":   "10000000011111110010000111011110", # 0x807F21DE
        "RED":      "10000000011111111001000101101110", # 0x807F916E
        "GREEN":    "10000000011111110001000111101110", # 0x807F11EE
        "BLUE":     "10000000011111110101000110101110", # 0x807F51AE
        "WHITE":    "10000000011111111101000100101110", # 0x807FD12E
        "FLASH":    "10000000011111111111000100001110", # 0x807FF10E
        "STROBE":   "10000000011111111110100100010110", # 0x807FE916
        "FADE":     "10000000011111111101100100100110", # 0x807FD926
        "SMOOTH":   "10000000011111111100100100110110", # 0x807FC936
        "RED1":     "10000000011111111011000101001110", # 0x807FB14E
        "ORANGE":   "10000000011111111010100101010110", # 0x807FA956
        "ORANGE1":  "10000000011111111001100101100110", # 0x807F9966
        "YELLOW":   "10000000011111111000100101110110", # 0x807F8976
        "GREEN1":   "10000000011111110011000111001110", # 0x807F31CE
        "CYAN":     "10000000011111110010100111010110", # 0x807F29D6
        "GREY":     "10000000011111110001100111100110", # 0x807F19E6
        "BLUE1":    "10000000011111110111000110001110", # 0x807F718E
        "BLUE2":    "10000000011111110000100111110110", # 0x807F09F6
        "MAGENTA":  "10000000011111110110100110010110", # 0x807F6996
        "MAGENTA1": "10000000011111110101100110100110", # 0x807F59A6
        "MAGENTA2": "10000000011111110100100110110110", # 0x807F49B6
      }
      curkey = led_keys.get(command, [])

    elif device == "TV": # Toshiba 42XV635D
      # bits:32, flags:SPACE_ENC|CONST_LENGTH, header:9018 4440
      # one: 619 1621, zero:619 507, ptrail: 620, repeat:9021 2185
      # gap: 107862, toggle_bit_mask:0x0
      leading_pulse_duration = 9000
      leading_gap_duration   = 4500
      one_pulse              = 619
      zero_pulse             = 619
      one_gap                = 1621
      zero_gap               = 507
      tv_keys = {
        "OK":          "00000010111111011000010001111011", # 0x02FD847B
        "UP":          "00000010111111011001100001100111", # 0x02FD9867
        "DOWN":        "00000010111111011011100001000111", # 0x02FDB847
        "RIGHT":       "00000010111111010000001011111101", # 0x02FD02FD
        "LEFT":        "00000010111111010100001010111101", # 0x02FD42BD
        "CH+":         "00000010111111011101100000100111", # 0x02FDD827
        "CH-":         "00000010111111011111100000000111", # 0x02FDF807
        "VOL+":        "00000010111111010101100010100111", # 0x02FD58A7
        "VOL-":        "00000010111111010111100010000111", # 0x02FD7887
        "MUTE":        "00000010111111010000100011110111", # 0x02FD08F7
        "1":           "00000010111111011000000001111111", # 0x02FD807F
        "2":           "00000010111111010100000010111111", # 0x02FD40BF
        "3":           "00000010111111011100000000111111", # 0x02FDC03F
        "4":           "00000010111111010010000011011111", # 0x02FD20DF
        "5":           "00000010111111011010000001011111", # 0x02FDA05F
        "6":           "00000010111111010110000010011111", # 0x02FD609F
        "7":           "00000010111111011110000000011111", # 0x02FDE01F
        "8":           "00000010111111010001000011101111", # 0x02FD10EF
        "9":           "00000010111111011001000001101111", # 0x02FD906F
        "0":           "00000010111111010000000011111111", # 0x02FD00FF
        "AD":          "00000010111111010100011010111001", # 0x02FD46B9
        "EXIT":        "00000010111111011100001000111101", # 0x02FDC23D
        "TEXT":        "00000010111111011110100000010111", # 0x02FDE817
        "SOURCE":      "00000010111111010010100011010111", # 0x02FD28D7
        "EXT1":        "00000010111111011000110001110011",
        "EXT2":        "00000010111111010100110010110011",
        "EXT3":        "00000010111111011100110000110011",
        "HDMI1":       "00000010111111010001110011100011",
        "HDMI2":       "00000010111111011001110001100011",
        "HDMI3":       "00000010111111010101110010100011",
        "HDMI4":       "00000010111111011101110000100011",
        "QUICK":       "00000010111111011100011000111001", # 0x02FDC639
        "GUIDE":       "00000010111111011010001001011101", # 0x02FDA25D
        "INFO":        "00000010111111010110100010010111", # 0x02FD6897
        "RETURN":      "00000010111111010010011011011001", # 0x02FD26D9
        "RED":         "00000010111111010001001011101101", # 0x02FD12ED
        "GREEN":       "00000010111111011001001001101101", # 0x02FD926D
        "YELLOW":      "00000010111111010101001010101101", # 0x02FD52AD
        "BLUE":        "00000010111111011101001000101101", # 0x02FDD22D
        "STEREO":      "00000010111111011100100000110111", # 0x02FDC837
        "SUBTITLE":    "00000010111111010011000011001111", # 0x02FD30CF
        "ASPECTRATIO": "00000010111111011001101001100101", # 0x02FD9A65
        "MENU":        "00000010111111011101101000100101", # 0x02FDDA25
        "TV":          "00000010111111010010001011011101", # 0x02FD22DD
        "LIST":        "00000010111111011110001000011101", # 0x02FDE21D
        "PAUSE":       "00000010111111010100010010111011", # 0x02FD44BB
        "PREVIOUS":    "00000010111111010010101011010101", # 0x02FD2AD5
        "REWIND":      "00000010111111011100101000110101", # 0x02FDCA35
        "FORWARD":     "00000010111111011011000001001111", # 0x02FDB04F
        "NEXT":        "00000010111111011010100001010111", # 0x02FDA857
        "POWER":       "00000010111111010100100010110111", # 0x02FD48B7
        "POWEROFF":    "00000010111111011111111000000001", # 0x02FDFE01
        "POWERON":     "00000010111111010111111010000001", # 0x02FD7E81
      }
      curkey = tv_keys.get(command, [])

    elif device == "DVD":
      # bits:12, flags:SPACE_ENC|CONST_LENGTH, header:2448 552
      # one:1240 557, zero:640 557, post_data_bits:8, post_data: 0x47
      # gap: 45087 -> 14400?, toggle_bit_mask:0x0
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
        "POWEROFF":  "11110100101101000111",
        "POWERON":   "01110100101101000111",
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
  subprocess.Popen(["sudo", "pigpiod", "-m"]) # start pigpiod using "Disable alerts (sampling)" for lower CPU usage
  set_rgb(state_rgb[state]) # initial update of RGB LED (should be "IDLE" state)
  threading.Thread(target=adb_connect, args=("firetv1",)).start()
  threading.Thread(target=eyetv_remote_input).start()
  threading.Thread(target=playstation_remote_input).start()
  threading.Thread(target=socket_input).start()

# sudo systemctl enable ir_hub.service -----------------------------------------
# ir_hub.service:
# [Unit]
# Description=Infraed hub with Python in mypitools
# After=network.target
#
# [Service]
# ExecStart=/usr/bin/python3 /home/pi/mypitools/ir_hub.py
# WorkingDirectory=/home/pi/mypitools/
# Restart=always
# User=root
# Environment=PYTHONUNBUFFERED=1
#
# [Install]
# WantedBy=multi-user.target
#
# send_ir.sh: ------------------------------------------------------------------
# #!/bin/bash
# echo -n "$1" | sudo socat - UNIX-CONNECT:/tmp/ir_hub_script.sock
#
# Disable lirc exclusive grabbing eyeTV remote: --------------------------------
# sudo vi /etc/udev/rules.d/99-ignore-eyetv.rules
# SUBSYSTEM=="input", ATTRS{idVendor}=="0fd9", ATTRS{idProduct}=="0018", ENV{eventlircd_enable}="false"
#
# sudo udevadm control --reload-rules
# sudo udevadm trigger
