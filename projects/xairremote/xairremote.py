


# control a Behringer XAIR mixer with a nanoKONTROL connected to a Raspberry Pi

import sys
sys.path.append('python-x32/src')
sys.path.append('python-x32/src/pythonx32')
from re import match
from alsa_midi import SequencerClient, WRITE_PORT, MidiBytesEvent, NoteOnEvent, NoteOffEvent
from pythonx32 import x32


def main():
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

  # initialize connection to Behringer mixer
  mixer = x32.BehringerX32("127.0.0.1", 10336, False)
  mixer.ping()

  # parse MIDI inevents
  try:
    MIDI_statusbyte = 0
    MIDI_databyte1  = 0
    MIDI_databyte2  = 0
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

        # TEST
        t = nanoKONTROL_MIDI_lookup()
        c = (MIDI_statusbyte, MIDI_databyte1)
        if c in t:
            channel = t[c][2] + 1
            if t[c][1] == "f": # fader
                value = [MIDI_databyte2 / 127]
                mixer.set_value(f'/ch/{channel:#02}/mix/fader', value, False)
                #print(f'/ch/{channel:#02}/mix/fader' {value})
            if t[c][1] == "d": # dial
                value = [MIDI_databyte2 / 127]
                mixer.set_value(f'/ch/{channel:#02}/mix/pan', value, False)
                #print(f'/ch/{channel:#02}/mix/pan {value}')

        #event_s = " ".join(f"{b}" for b in event.midi_bytes)
        #print(f"{event_s}")
  except KeyboardInterrupt:
    pass

def nanoKONTROL_MIDI_lookup():
    # (scene, type, value), types: "f" is fader, "d" is dial
    return {(0XB0, 2): (0, "f", 0), (0XB0, 3): (0, "f", 1), (0XB0,  4): (0, "f", 2), (0XB0,  5): (0, "f", 3), (0XB0, 6): (0, "f", 4),
            (0XB0, 8): (0, "f", 5), (0XB0, 9): (0, "f", 6), (0XB0, 12): (0, "f", 7), (0XB0, 13): (0, "f", 8),
            (0XB0, 14): (0, "d", 0), (0XB0, 15): (0, "d", 1), (0XB0, 16): (0, "d", 2), (0XB0, 17): (0, "d", 3), (0XB0, 18): (0, "d", 4),
            (0XB0, 19): (0, "d", 5), (0XB0, 20): (0, "d", 6), (0XB0, 21): (0, "d", 7), (0XB0, 22): (0, "d", 8),

            (0XB0, 42): (1, "f", 0), (0XB0, 43): (1, "f", 1), (0XB0, 50): (1, "f", 2), (0XB0, 51): (1, "f", 3), (0XB0, 52): (1, "f", 4),
            (0XB0, 53): (1, "f", 5), (0XB0, 54): (1, "f", 6), (0XB0, 55): (1, "f", 7), (0XB0, 56): (1, "f", 8),
            (0XB0, 57): (1, "d", 0), (0XB0, 58): (1, "d", 1), (0XB0, 59): (1, "d", 2), (0XB0, 60): (1, "d", 3), (0XB0, 61): (1, "d", 4),
            (0XB0, 62): (1, "d", 5), (0XB0, 63): (1, "d", 6), (0XB0, 65): (1, "d", 7), (0XB0, 66): (1, "d", 8),

            (0XB0,  85): (2, "f", 0), (0XB0,  86): (2, "f", 1), (0XB0,  87): (2, "f", 2), (0XB0,  88): (2, "f", 3), (0XB0,  89): (2, "f", 4),
            (0XB0,  90): (2, "f", 5), (0XB0,  91): (2, "f", 6), (0XB0,  92): (2, "f", 7), (0XB0,  93): (2, "f", 8),
            (0XB0,  94): (2, "d", 0), (0XB0,  95): (2, "d", 1), (0XB0,  96): (2, "d", 2), (0XB0,  97): (2, "d", 3), (0XB0, 102): (2, "d", 4),
            (0XB0, 103): (2, "d", 5), (0XB0, 104): (2, "d", 6), (0XB0, 105): (2, "d", 7), (0XB0, 106): (2, "d", 8),

            (0XB0,  7): (3, "f", 0), (0XB1,  7): (3, "f", 1), (0XB2,  7): (3, "f", 2), (0XB3,  7): (3, "f", 3), (0XB4,  7): (3, "f", 4),
            (0XB5,  7): (3, "f", 5), (0XB6,  7): (3, "f", 6), (0XB7,  7): (3, "f", 7), (0XB8,  7): (3, "f", 8),
            (0XB0, 10): (3, "d", 0), (0XB1, 10): (3, "d", 1), (0XB2, 10): (3, "d", 2), (0XB3, 10): (3, "d", 3), (0XB4, 10): (3, "d", 4),
            (0XB5, 10): (3, "d", 5), (0XB6, 10): (3, "d", 6), (0XB7, 10): (3, "d", 7), (0XB8, 10): (3, "d", 8)}

if __name__ == '__main__':
    main()


