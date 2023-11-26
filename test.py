import midi
import rtmidi

midi_in, midi_out = midi.get_ports()

port = rtmidi.MidiOut()
port.open_port(midi.get_outidx())
msg = [0xCB,0x1B]
port.send_message(msg)
