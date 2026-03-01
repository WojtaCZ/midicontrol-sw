import midi
import comm
import config

# Load the configuration file
config.load()

midi_in, midi_out = midi.get_ports()

# Initialize the communication with the controller 
comm.init()

