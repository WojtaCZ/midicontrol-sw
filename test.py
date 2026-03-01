import midi

midi_in, midi_out = midi.get_ports()

midi.notes_off()