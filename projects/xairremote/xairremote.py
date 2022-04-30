


# control a Behringer XAIR mixer with a nanoKONTROL connected to a Raspberry Pi

from re import match
from alsa_midi import SequencerClient, WRITE_PORT, MidiBytesEvent, NoteOnEvent, NoteOffEvent


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

  # parse MIDI inevents
  try:
    while True:
      event = client.event_input(prefer_bytes = True)
      if event is not None and isinstance(event, MidiBytesEvent):
        event_s = " ".join(f"{b:02X}" for b in event.midi_bytes)
        print(f"{event.source!s:7} {event_s}")
  except KeyboardInterrupt:
    pass


if __name__ == '__main__':
    main()


