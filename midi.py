import rtmidi


def get_ports():
    #Retreive available midi interfaces
    outports = rtmidi.MidiOut().get_ports()
    inports = rtmidi.MidiIn().get_ports()

    global outport
    outport = None
    global inport 
    inport = None

    key = "MIDIControl"

    #Search through array and find the output port
    for port in outports:
        if port.find(key) > -1:
            outport = port

    if outport != None:
        print("Found MIDIControl output port: " + str(outport))
    else:
        print("MIDIControl output port not found!")
        exit(1)

    #Search through array and find the input port
    for port in inports:
        if port.find(key) > -1:
            inport = port

    if inport != None:
        print("Found MIDIControl input port: " + str(inport))
    else:
        print("MIDIControl input port not found!")
        exit(1)

    return inport, outport

def get_inport():
    return inport

def get_outport():
    return outport