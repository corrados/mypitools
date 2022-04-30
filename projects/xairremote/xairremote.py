


# control a Behringer XAIR mixer with a nanoKONTROL connected to a Raspberry Pi

from re import match
from alsa_midi import SequencerClient, WRITE_PORT, NoteOnEvent, NoteOffEvent

# setup the MIDI sequencer client for xairremote
client = SequencerClient("xairremote")
port   = client.create_port("output", caps=WRITE_PORT)

# get the nanoKONTROL ALSA midi port
dest_ports    = client.list_ports(output=True)
filtered_port = list(filter(lambda cur_port: match('\W*(nanoKONTROL)\W*', cur_port.name), dest_ports))
if len(filtered_port) == 0:
  raise Exception('No nanoKONTROL MIDI device found. Is the nanoKONTROL connected?')
dest_port = filtered_port[0];

# TODO from here...
print(filtered_port[0].name)



