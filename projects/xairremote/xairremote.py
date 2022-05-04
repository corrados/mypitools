#******************************************************************************
# * Copyright (c) 2022
# * Author: Volker Fischer
# *****************************************************************************
# * This program is free software; you can redistribute it and/or modify it under
# * the terms of the GNU General Public License as published by the Free Software
# * Foundation; either version 2 of the License, or (at your option) any later
# * version.
# * This program is distributed in the hope that it will be useful, but WITHOUT
# * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# * details.
# * You should have received a copy of the GNU General Public License along with
# * this program; if not, write to the Free Software Foundation, Inc.,
# * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#******************************************************************************

# control a Behringer XAIR mixer with a nanoKONTROL connected to a Raspberry Pi
import sys
sys.path.append('python-x32/src')
sys.path.append('python-x32/src/pythonx32')
from re import match
import threading
import time
import socket
from alsa_midi import SequencerClient, WRITE_PORT, MidiBytesEvent
from pythonx32 import x32

found_addr = -1

def main():
  global found_addr

  # setup the MIDI sequencer client for xairremote
  client = SequencerClient("xairremote")
  port   = client.create_port("output", caps = WRITE_PORT)
  queue  = client.create_queue()
  queue.start()
  client.drain_output()

  # get the nanoKONTROL ALSA midi port
  dest_ports    = client.list_ports(output = True)
  filtered_port = list(filter(lambda cur_port: match('\W*(nanoKONTROL)\W*', cur_port.name), dest_ports))
  if len(filtered_port) == 0:
    raise Exception('No nanoKONTROL MIDI device found. Is the nanoKONTROL connected?')
  nanoKONTROL_port = filtered_port[0];
  port.connect_from(nanoKONTROL_port)

  try:
    # search for a mixer and initialize the connection to the mixer
    local_port  = 10300
    addr_subnet = '.'.join(get_ip().split('.')[0:3]) # only use first three numbers of local IP address

    while found_addr < 0:
      for i in range(2, 255):
        threading.Thread(target = try_to_ping_mixer, args = (addr_subnet, local_port + 1, i, )).start()
      time.sleep(2) # time-out is 1 second -> wait two-times the time-out

    mixer = x32.BehringerX32(f"{addr_subnet}.{found_addr}", local_port, False)

    # query all current fader values
    bus_ch = 5; # define here the bus channel you want to control
    fader_init_val = [0] * 9 # nanoKONTROL has 9 faders
    bus_init_val   = [0] * 9
    for i in range(8):
      fader_init_val[i] = mixer.get_value(f'/ch/{i + 1:#02}/mix/fader')[0]
      bus_init_val[i]   = mixer.get_value(f'/ch/{i + 1:#02}/mix/{bus_ch:#02}/level')[0]

    # parse MIDI inevents
    MIDI_statusbyte = 0
    MIDI_table      = nanoKONTROL_MIDI_lookup() # create MIDI table for nanoKONTROL
    while True:
      event = client.event_input(prefer_bytes = True)
      if event is not None and isinstance(event, MidiBytesEvent):
        if len(event.midi_bytes) == 3:
            # status byte has changed
            MIDI_statusbyte = event.midi_bytes[0]
            MIDI_databyte1  = event.midi_bytes[1]
            MIDI_databyte2  = event.midi_bytes[2]
        elif len(event.midi_bytes) == 2:
            MIDI_databyte1  = event.midi_bytes[0]
            MIDI_databyte2  = event.midi_bytes[1]

        # send corresponding OSC commands to the mixer
        c = (MIDI_statusbyte, MIDI_databyte1)
        if c in MIDI_table:
            channel = MIDI_table[c][2] + 1

            if MIDI_table[c][0] == 0 and MIDI_table[c][1] == "f": # fader in first SCENE
                value     = MIDI_databyte2 / 127
                ini_value = fader_init_val[channel - 1]
                # only apply value if current fader value is not too far off
                if ini_value < 0 or (ini_value >= 0 and abs(ini_value - value) < 0.1):
                    fader_init_val[channel - 1] = -1 # invalidate initial value
                    mixer.set_value(f'/ch/{channel:#02}/mix/fader', [value], False)

            if MIDI_table[c][0] == 1 and MIDI_table[c][1] == "f": # bus fader in second SCENE
                value     = MIDI_databyte2 / 127
                ini_value = bus_init_val[channel - 1]
                # only apply value if current fader value is not too far off
                if ini_value < 0 or (ini_value >= 0 and abs(ini_value - value) < 0.1):
                    bus_init_val[channel - 1] = -1 # invalidate initial value
                    mixer.set_value(f'/ch/{channel:#02}/mix/{bus_ch:#02}/level', [value], False)

            if MIDI_table[c][0] == 3 and MIDI_table[c][1] == "d": # dial in last SCENE
                value = MIDI_databyte2 / 127
                mixer.set_value(f'/ch/{channel:#02}/mix/pan', [value], False)

        #event_s = " ".join(f"{b}" for b in event.midi_bytes)
        #print(f"{event_s}")
  except KeyboardInterrupt:
    pass

def try_to_ping_mixer(addr_subnet, start_port, i):
    global found_addr
    search_mixer = x32.BehringerX32(f"{addr_subnet}.{i}", start_port + i, False)
    search_mixer._timeout = 1 # just one second time-out
    try:
        search_mixer.ping()
        found_addr = i
    except:
        pass # no mixer found -> do nothing
    finally:
        search_mixer.__del__()

# taken from stack overflow "Finding local IP addresses using Python's stdlib"
def get_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        IP = s.getsockname()[0]
    except Exception:
        IP = '127.0.0.1'
    finally:
        s.close()
    return IP

def nanoKONTROL_MIDI_lookup():
    # (scene, type, value), types: "f" is fader, "d" is dial, "b1" is button 1, "b2" is button 2
    return {(0XB0,  2): (0, "f",  0), (0XB0,  3): (0, "f",  1), (0XB0,  4): (0, "f",  2), (0XB0,  5): (0, "f",  3), (0XB0,  6): (0, "f",  4),
            (0XB0,  8): (0, "f",  5), (0XB0,  9): (0, "f",  6), (0XB0, 12): (0, "f",  7), (0XB0, 13): (0, "f",  8),
            (0XB0, 14): (0, "d",  0), (0XB0, 15): (0, "d",  1), (0XB0, 16): (0, "d",  2), (0XB0, 17): (0, "d",  3), (0XB0, 18): (0, "d",  4),
            (0XB0, 19): (0, "d",  5), (0XB0, 20): (0, "d",  6), (0XB0, 21): (0, "d",  7), (0XB0, 22): (0, "d",  8),
            (0XB0, 33): (0, "b1", 0), (0XB0, 34): (0, "b1", 1), (0XB0, 35): (0, "b1", 2), (0XB0, 36): (0, "b1", 3), (0XB0, 37): (0, "b1", 4),
            (0XB0, 38): (0, "b1", 5), (0XB0, 39): (0, "b1", 6), (0XB0, 40): (0, "b1", 7), (0XB0, 41): (0, "b1", 8),
            (0XB0, 23): (0, "b2", 0), (0XB0, 24): (0, "b2", 1), (0XB0, 25): (0, "b2", 2), (0XB0, 26): (0, "b2", 3), (0XB0, 27): (0, "b2", 4),
            (0XB0, 28): (0, "b2", 5), (0XB0, 29): (0, "b2", 6), (0XB0, 30): (0, "b2", 7), (0XB0, 31): (0, "b2", 8),

            (0XB0, 42): (1, "f",  0), (0XB0, 43): (1, "f",  1), (0XB0, 50): (1, "f",  2), (0XB0, 51): (1, "f",  3), (0XB0, 52): (1, "f",  4),
            (0XB0, 53): (1, "f",  5), (0XB0, 54): (1, "f",  6), (0XB0, 55): (1, "f",  7), (0XB0, 56): (1, "f",  8),
            (0XB0, 57): (1, "d",  0), (0XB0, 58): (1, "d",  1), (0XB0, 59): (1, "d",  2), (0XB0, 60): (1, "d",  3), (0XB0, 61): (1, "d",  4),
            (0XB0, 62): (1, "d",  5), (0XB0, 63): (1, "d",  6), (0XB0, 65): (1, "d",  7), (0XB0, 66): (1, "d",  8),
            (0XB0, 76): (1, "b1", 0), (0XB0, 77): (1, "b1", 1), (0XB0, 78): (1, "b1", 2), (0XB0, 79): (1, "b1", 3), (0XB0, 80): (1, "b1", 4),
            (0XB0, 81): (1, "b1", 5), (0XB0, 82): (1, "b1", 6), (0XB0, 83): (1, "b1", 7), (0XB0, 84): (1, "b1", 8),
            (0XB0, 67): (1, "b2", 0), (0XB0, 68): (1, "b2", 1), (0XB0, 69): (1, "b2", 2), (0XB0, 70): (1, "b2", 3), (0XB0, 71): (1, "b2", 4),
            (0XB0, 72): (1, "b2", 5), (0XB0, 73): (1, "b2", 6), (0XB0, 74): (1, "b2", 7), (0XB0, 75): (1, "b2", 8),

            (0XB0,  85): (2, "f",  0), (0XB0,  86): (2, "f",  1), (0XB0,  87): (2, "f",  2), (0XB0,  88): (2, "f",  3), (0XB0,  89): (2, "f",  4),
            (0XB0,  90): (2, "f",  5), (0XB0,  91): (2, "f",  6), (0XB0,  92): (2, "f",  7), (0XB0,  93): (2, "f",  8),
            (0XB0,  94): (2, "d",  0), (0XB0,  95): (2, "d",  1), (0XB0,  96): (2, "d",  2), (0XB0,  97): (2, "d",  3), (0XB0, 102): (2, "d",  4),
            (0XB0, 103): (2, "d",  5), (0XB0, 104): (2, "d",  6), (0XB0, 105): (2, "d",  7), (0XB0, 106): (2, "d",  8),
            (0XB0, 116): (2, "b1", 0), (0XB0, 117): (2, "b1", 1), (0XB0, 118): (2, "b1", 2), (0XB0, 119): (2, "b1", 3), (0XB0, 120): (2, "b1", 4),
            (0XB0, 121): (2, "b1", 5), (0XB0, 122): (2, "b1", 6), (0XB0, 123): (2, "b1", 7), (0XB0, 124): (2, "b1", 8),
            (0XB0, 107): (2, "b2", 0), (0XB0, 108): (2, "b2", 1), (0XB0, 109): (2, "b2", 2), (0XB0, 110): (2, "b2", 3), (0XB0, 111): (2, "b2", 4),
            (0XB0, 112): (2, "b2", 5), (0XB0, 113): (2, "b2", 6), (0XB0, 114): (2, "b2", 7), (0XB0, 115): (2, "b2", 8),

            (0XB0,  7): (3, "f",  0), (0XB1,  7): (3, "f",  1), (0XB2,  7): (3, "f",  2), (0XB3,  7): (3, "f",  3), (0XB4,  7): (3, "f",  4),
            (0XB5,  7): (3, "f",  5), (0XB6,  7): (3, "f",  6), (0XB7,  7): (3, "f",  7), (0XB8,  7): (3, "f",  8),
            (0XB0, 10): (3, "d",  0), (0XB1, 10): (3, "d",  1), (0XB2, 10): (3, "d",  2), (0XB3, 10): (3, "d",  3), (0XB4, 10): (3, "d",  4),
            (0XB5, 10): (3, "d",  5), (0XB6, 10): (3, "d",  6), (0XB7, 10): (3, "d",  7), (0XB8, 10): (3, "d",  8),
            #(0XB0, 17): (3, "b1", 0), # overlaps with first set fourth dial
            (0XB1, 17): (3, "b1", 1), (0XB2, 17): (3, "b1", 2), (0XB3, 17): (3, "b1", 3), (0XB4, 17): (3, "b1", 4),
            (0XB5, 17): (3, "b1", 5), (0XB6, 17): (3, "b1", 6), (0XB7, 17): (3, "b1", 7), (0XB8, 17): (3, "b1", 8),
            #(0XB0, 16): (3, "b2", 0), # overlaps with first set third dial
            (0XB1, 16): (3, "b2", 1), (0XB2, 16): (3, "b2", 2), (0XB3, 16): (3, "b2", 3), (0XB4, 16): (3, "b2", 4),
            (0XB5, 16): (3, "b2", 5), (0XB6, 16): (3, "b2", 6), (0XB7, 16): (3, "b2", 7), (0XB8, 16): (3, "b2", 8)}

if __name__ == '__main__':
    main()


