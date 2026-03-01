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

import protocol


player = None
recorder = None


class CommHandler:
    """Správce binární komunikace přes protokol MIDIControl."""

    def __init__(self, writer):
        self.writer = writer
        self.parser = protocol.PacketParser()
        self.packet_id = 0
        self.pending_acks = {}  # packet_id → (timestamp, retries, msg_type, dest, payload)

    def next_packet_id(self):
        pid = self.packet_id
        self.packet_id = (self.packet_id + 1) & 0xFF
        return pid

    def send(self, dest, msg_type, payload=b''):
        """Odešle paket bez čekání na ACK."""
        pkt = protocol.Packet(
            source=protocol.Addr.PC,
            dest=dest,
            packet_id=self.next_packet_id(),
            msg_type=msg_type,
            payload=payload,
        )
        raw = protocol.encode_packet(pkt)
        self.writer.write(raw)
        return pkt.packet_id

    def send_with_ack(self, dest, msg_type, payload=b''):
        """Odešle paket a zaregistruje čekání na ACK."""
        pid = self.send(dest, msg_type, payload)
        self.pending_acks[pid] = (time.time(), 0, msg_type, dest, payload)
        return pid

    def process_byte(self, byte):
        """Zpracuje jeden přijatý bajt. Vrací Packet nebo None."""
        pkt = self.parser.feed(byte)
        if pkt is not None:
            self.handle_packet(pkt)
        return pkt

    def handle_packet(self, pkt):
        """Směrování přijatého paketu."""
        msg = pkt.msg_type

        if msg == protocol.MsgType.ACK:
            if len(pkt.payload) >= 1:
                acked_id = pkt.payload[0]
                self.pending_acks.pop(acked_id, None)
                print(f"ACK přijat pro PacketID={acked_id:#04x}")

        elif msg == protocol.MsgType.NAK:
            if len(pkt.payload) >= 2:
                nacked_id = pkt.payload[0]
                reason = pkt.payload[1]
                self.pending_acks.pop(nacked_id, None)
                print(f"NAK přijat pro PacketID={nacked_id:#04x}, důvod={reason:#04x}")

        elif msg == protocol.MsgType.SIGNAL_PLAYING:
            song = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
            print(f"Přehrávání: {song}")

        elif msg == protocol.MsgType.SIGNAL_RECORDING:
            filename = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
            print(f"Nahrávání: {filename}")

        elif msg == protocol.MsgType.SIGNAL_STOPPED:
            print("Zastaveno")

        elif msg == protocol.MsgType.SONG_LIST_RESP:
            song_list = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
            songs = song_list.split(';')
            print(f"Seznam písní: {songs}")

        elif msg == protocol.MsgType.STATUS_RESP:
            if len(pkt.payload) >= 3:
                ble_connected = bool(pkt.payload[0])
                current_on = bool(pkt.payload[1])
                display_connected = bool(pkt.payload[2])
                print(f"Status: BLE={ble_connected}, current={current_on}, display={display_connected}")

        else:
            print(f"Neznámý paket: MsgType={msg:#04x}")

    def check_timeouts(self):
        """Kontrola timeoutů a opakování nepotvrzených paketů."""
        now = time.time()
        to_remove = []

        for pid, (timestamp, retries, msg_type, dest, payload) in self.pending_acks.items():
            if now - timestamp > protocol.RETRY_TIMEOUT:
                if retries < protocol.MAX_RETRIES:
                    # Opakování
                    pkt = protocol.Packet(
                        source=protocol.Addr.PC,
                        dest=dest,
                        packet_id=pid,
                        msg_type=msg_type,
                        payload=payload,
                    )
                    raw = protocol.encode_packet(pkt)
                    self.writer.write(raw)
                    self.pending_acks[pid] = (now, retries + 1, msg_type, dest, payload)
                    print(f"Retry #{retries + 1} pro PacketID={pid:#04x}")
                else:
                    print(f"Timeout pro PacketID={pid:#04x} po {protocol.MAX_RETRIES} pokusech")
                    to_remove.append(pid)

        for pid in to_remove:
            del self.pending_acks[pid]

    # --- Pohodlné funkce ---

    def play_song(self, song_name):
        global player
        self.organOn()
        time.sleep(1)
        player = subprocess.Popen(
            ["aplaymidi", "-p", str(midi.get_outport().split(" ")[-1]),
             config.playFolder + "/" + song_name + '.mid'],
            shell=False
        )
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.PLAY_SONG,
            (song_name + '\x00').encode('utf-8')
        )

    def stop_song(self):
        global player, recorder
        self.organOff()
        if player is not None:
            os.kill(player.pid, signal.SIGTERM)
            player.terminate()
            midi.notes_off()
            player = None
        if recorder is not None:
            os.kill(recorder.pid, signal.SIGTERM)
            recorder.terminate()
            recorder = None
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.STOP_SONG
        )

    def record_song(self):
        global recorder
        time.sleep(1)
        self.organOn()
        time.sleep(1)
        filename = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.mid")
        recorder = subprocess.Popen(
            ["arecordmidi", "-p", str(midi.get_inport().split(" ")[-1]),
             config.recFolder + "/" + filename],
            shell=False
        )
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.RECORD_SONG,
            (filename + '\x00').encode('utf-8')
        )

    def get_song_list(self):
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.GET_SONG_LIST
        )

    def organOn(self):
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.SET_CURRENT,
            bytes([0x01])
        )

    def organOff(self):
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.SET_CURRENT,
            bytes([0x00])
        )


# --- Globální handler ---
handler = None


async def comm_loop(port):
    global handler
    reader, writer = await open_serial_connection(
        url='/dev/' + str(port.name), baudrate=115200
    )
    print("Opened serial communication port")

    handler = CommHandler(writer)

    while True:
        data = await reader.read(256)
        for byte in data:
            handler.process_byte(byte)
        handler.check_timeouts()


def get_port():
    key = "VID:PID=6666:0001"
    comport = None

    for port in serial.tools.list_ports.comports():
        if port.hwid.find(key) > -1:
            comport = port

    if comport is None:
        print("MIDIControl COM port not found!")
    else:
        print("Found MIDIControl COM port: " + str(comport.name))

    return comport


def init():
    comport = get_port()

    if comport is not None:
        loop = get_event_loop()
        loop.run_until_complete(comm_loop(comport))
