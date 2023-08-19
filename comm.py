import serial.tools.list_ports
from asyncio import get_event_loop
from serial_asyncio import open_serial_connection
import os
import config
import signal
import midi
import subprocess
import time
from datetime import datetime

player = None
recorder = None

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
    global player
    global recorder
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
            organOn()
            time.sleep(1)
            player = subprocess.Popen(["aplaymidi", "-p", str(midi.get_outport().split(" ")[-1]), config.playFolder + "/" + msg[11:] + '.mid'], shell=False)
    if msg[:15] == "music bind play":
        if type == "set":
            try:
                file = config.getBinding("Bind" + msg[16:17])
                print(file)
                organOn()
                time.sleep(1)
                player = subprocess.Popen(["aplaymidi", "-p", str(midi.get_outport().split(" ")[-1]), config.playFolder + "/" + file.replace(".mid", "") + '.mid'], shell=False)
            except:
                print("Could't find the requested binding.")
    if msg[:10] == "music stop":
        if type == "set":
            organOff()
            print("STOP")
            if player != None:
                os.kill(player.pid, signal.SIGTERM)
                player.terminate()
                midi.notes_off()
                player = None

            if recorder != None:
                os.kill(recorder.pid, signal.SIGTERM)
                recorder.terminate()
                recorder = None
            
    if msg[:12] == "music record":
        if type == "set":
            time.sleep(1)
            organOn()
            time.sleep(1)
            filename = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.mid")
            recorder = subprocess.Popen(["arecordmidi", "-p", str(midi.get_inport().split(" ")[-1]), config.recFolder + "/" + filename.replace(".mid", "") + '.mid'], shell=False)
            send("set music record " + filename)
            

            

            
def send(msg):
    print("Sending: " + msg)
    writer.write(bytes(msg + "\r\n", encoding="utf-8"))

def organOn():
    send("set base current on")

def organOff():
    send("set base current off")