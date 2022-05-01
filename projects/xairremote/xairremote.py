


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

  # TEST
  mixer = x32.BehringerX32("127.0.0.1", 10330, False)
  mixer.ping()
  #mixer.set_value("/ch/01/config/name", "TEST")
  #mixer.set_value("/ch/01/mix/fader", 0.5)

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

        if MIDI_statusbyte == 0XB0:
            channel = MIDI_databyte1
            mixer.set_value(f'/ch/{channel:#02}/mix/fader', [MIDI_databyte2 / 127], False)
            print(f'/ch/{channel:#02}/mix/fader')

        event_s = " ".join(f"{b:02X}" for b in event.midi_bytes)
        print(f"{event.source!s:7} {event_s}")
  except KeyboardInterrupt:
    pass


if __name__ == '__main__':
    main()


