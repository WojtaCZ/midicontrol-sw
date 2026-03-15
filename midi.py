import rtmidi


def get_ports():
    # Retrieve available midi interfaces
    outports = rtmidi.MidiOut().get_ports()
    inports = rtmidi.MidiIn().get_ports()

    global outport
    global outportIdx
    outportIdx = -1
    outport = None
    global inport
    global inportIdx
    inportIdx = -1
    inport = None

    key = "MIDIControl"

    # Search through array and find the output port
    for i, port in enumerate(outports):
        if port.find(key) > -1:
            outport = port
            outportIdx = i

    if outport is not None:
        print("Found MIDIControl output port: " + str(outport))
    else:
        print("MIDIControl output port not found!")
        exit(1)

    # Search through array and find the input port
    for i, port in enumerate(inports):
        if port.find(key) > -1:
            inport = port
            inportIdx = i

    if inport is not None:
        print("Found MIDIControl input port: " + str(inport))
    else:
        print("MIDIControl input port not found!")
        exit(1)

    return inport, outport

def get_inport():
    return inport

def get_outport():
    return outport

def get_outport_index():
    return outportIdx

def notes_off():
    port = rtmidi.MidiOut()
    port.open_port(outportIdx)
    for i in range(15):
        msg = [0b10110000 | i, 123, 0]
        port.send_message(msg)
    port.close_port()
