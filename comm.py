import serial.tools.list_ports
from asyncio import get_event_loop
from serial_asyncio import open_serial_connection
import os
import config
import midi

async def comm_loop(port):
    global reader
    global writer
    reader, writer = await open_serial_connection(url='/dev/' + str(port.name), baudrate=115200)
    print("Opened serial communication port")

    while True:
        line = await reader.readline()
        print(line)
        decode(str(line, 'utf-8'))

def get_port():
    key = "VID:PID=6666:0001"
    comport = None

    for port in serial.tools.list_ports.comports():
        if port.hwid.find(key) > -1:
            comport = port

    if comport == None:
        print("MIDIControl COM port not found!")
    else:
        print("Found MIDIControl COM port: " + str(comport.name))
    
    return comport

def init():
    comport = get_port()

    if comport != None:
        loop = get_event_loop()
        loop.run_until_complete(comm_loop(comport))

def decode(msg):
    type = msg[0:3]
    msg = msg[4:-2]

    print(type)
    print(msg[:9])


    if msg == "music songlist":
        if type == "get":
            # List the files in folder
            files = os.listdir(config.getPlayFolder())
            fileString = ";".join(files).replace(".mid", "")
            send(fileString)
    if msg[:10] == "music play":
        if type == "set":
            # Play the selected file with aplaymidi
            os.system("aplaymidi -p " + str(midi.get_outport().split(" ")[-1]) + config.playFolder + "/" + msg[11:-2] + ".mid")


def send(msg):
    print("Sending: " + msg)
    writer.write(bytes(msg + "\r\n", encoding="utf-8"))