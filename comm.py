import serial
import serial.tools.list_ports
import threading
import os
import signal
import subprocess
import time
from datetime import datetime

import protocol
import config
import midi
from player import MidiPlayer, DisplayInfo


_player = None
recorder = None


class CommHandler:
    """Správce binární komunikace přes protokol MIDIControl."""

    def __init__(self):
        self.serial = None
        self.parser = protocol.PacketParser()
        self.packet_id = 0
        self.pending_acks = {}  # packet_id → (timestamp, retries, msg_type, dest, payload)
        self._lock = threading.Lock()
        self._reader_thread = None
        self._keepalive_thread = None
        self._running = False
        self._callbacks = []  # list of (msg_type, callback) or (None, callback) for all
        self._last_base_keepalive = 0.0  # čas posledního KEEPALIVE_ACK z Base

    def connect(self):
        """Najde a otevře sériový port MIDIControl."""
        port = get_port()
        if port is None:
            return False
        self.serial = serial.Serial('/dev/' + str(port.name), baudrate=115200, timeout=0.1)
        print(f"Otevřen sériový port: {port.name}")
        self._running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()
        self._last_base_keepalive = time.time()
        self._keepalive_thread = threading.Thread(target=self._keepalive_loop, daemon=True)
        self._keepalive_thread.start()
        return True

    def disconnect(self):
        """Zavře sériový port."""
        self._running = False
        if self._reader_thread:
            self._reader_thread.join(timeout=2)
        if self._keepalive_thread:
            self._keepalive_thread.join(timeout=3)
        if self.serial and self.serial.is_open:
            self.serial.close()

    def on_packet(self, callback, msg_type=None):
        """Registrace callbacku pro příjem paketů. msg_type=None pro všechny."""
        self._callbacks.append((msg_type, callback))

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
        with self._lock:
            if self.serial and self.serial.is_open:
                self.serial.write(raw)
        return pkt.packet_id

    def send_with_ack(self, dest, msg_type, payload=b''):
        """Odešle paket a zaregistruje čekání na ACK."""
        pid = self.send(dest, msg_type, payload)
        with self._lock:
            self.pending_acks[pid] = (time.time(), 0, msg_type, dest, payload)
        return pid

    def _reader_loop(self):
        """Pozadí: čte bajty ze sériového portu a zpracovává pakety."""
        while self._running:
            try:
                if self.serial and self.serial.is_open and self.serial.in_waiting > 0:
                    data = self.serial.read(self.serial.in_waiting)
                    for byte in data:
                        pkt = self.parser.feed(byte)
                        if pkt is not None:
                            self._dispatch_packet(pkt)
                else:
                    time.sleep(0.01)

                self.check_timeouts()
            except serial.SerialException:
                print("Chyba sériového portu!")
                self._running = False
                break

    def _dispatch_packet(self, pkt):
        """Směrování přijatého paketu na handlery a callbacky."""
        self._handle_internal(pkt)
        for msg_type_filter, callback in self._callbacks:
            if msg_type_filter is None or msg_type_filter == pkt.msg_type:
                callback(pkt)

    def _handle_internal(self, pkt):
        """Interní zpracování ACK/NAK/KEEPALIVE_ACK."""
        msg = pkt.msg_type
        if msg == protocol.MsgType.ACK:
            if len(pkt.payload) >= 1:
                acked_id = pkt.payload[0]
                with self._lock:
                    self.pending_acks.pop(acked_id, None)
        elif msg == protocol.MsgType.NAK:
            if len(pkt.payload) >= 2:
                nacked_id = pkt.payload[0]
                with self._lock:
                    self.pending_acks.pop(nacked_id, None)
        elif msg == protocol.MsgType.KEEPALIVE_ACK:
            self._last_base_keepalive = time.time()

    def _keepalive_loop(self):
        """Periodicky odesílá KEEPALIVE na Base každé 2 sekundy."""
        while self._running:
            self.send(protocol.Addr.BASE, protocol.MsgType.KEEPALIVE)
            for _ in range(20):  # 20 × 100ms = 2s, ale kontrolujeme _running
                if not self._running:
                    break
                time.sleep(0.1)

    def is_base_alive(self):
        """Vrací True, pokud Base odpověděl KEEPALIVE_ACK v posledních 5 sekundách."""
        return (time.time() - self._last_base_keepalive) < 5.0

    def check_timeouts(self):
        """Kontrola timeoutů a opakování nepotvrzených paketů."""
        now = time.time()
        to_remove = []

        with self._lock:
            items = list(self.pending_acks.items())

        for pid, (timestamp, retries, msg_type, dest, payload) in items:
            if now - timestamp > protocol.RETRY_TIMEOUT:
                if retries < protocol.MAX_RETRIES:
                    pkt = protocol.Packet(
                        source=protocol.Addr.PC,
                        dest=dest,
                        packet_id=pid,
                        msg_type=msg_type,
                        payload=payload,
                    )
                    raw = protocol.encode_packet(pkt)
                    with self._lock:
                        if self.serial and self.serial.is_open:
                            self.serial.write(raw)
                        self.pending_acks[pid] = (now, retries + 1, msg_type, dest, payload)
                    print(f"Retry #{retries + 1} pro PacketID={pid:#04x}")
                else:
                    print(f"Timeout pro PacketID={pid:#04x} po {protocol.MAX_RETRIES} pokusech")
                    to_remove.append(pid)

        if to_remove:
            with self._lock:
                for pid in to_remove:
                    self.pending_acks.pop(pid, None)

    # --- Pohodlné funkce ---

    def play_song(self, song_name):
        global _player
        self.organOn()
        time.sleep(1)

        _player = MidiPlayer(midi.get_outport_index())
        _player.on_display_info = lambda info: self.send_now_playing_info(info)
        _player.on_finished = lambda: self._on_playback_finished()
        _player.play(config.playFolder + "/" + song_name + '.mid')

        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.PLAY_SONG,
            (song_name + '\x00').encode('utf-8')
        )

    def stop_song(self):
        global _player, recorder
        if _player is not None:
            _player.stop()
            _player = None
        if recorder is not None:
            try:
                os.kill(recorder.pid, signal.SIGTERM)
                recorder.terminate()
            except ProcessLookupError:
                pass
            recorder = None
        self.organOff()
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.STOP_SONG
        )

    def _on_playback_finished(self):
        """Callback volaný z MidiPlayer po dokončení přehrávání."""
        global _player
        _player = None
        self.organOff()
        self.send_with_ack(
            protocol.Addr.BASE,
            protocol.MsgType.STOP_SONG
        )

    def send_now_playing_info(self, info: DisplayInfo):
        """Odešle NOW_PLAYING_INFO na Base (bez ACK — časté aktualizace)."""
        # Kódování song do 2 bajtů BCD
        song = info.song
        d3 = (song // 1000) % 10
        d2 = (song // 100) % 10
        d1 = (song // 10) % 10
        d0 = song % 10
        song_h = (d3 << 4) | d2
        song_l = (d1 << 4) | d0

        # Verse BCD
        v1 = (info.verse // 10) % 10
        v0 = info.verse % 10
        verse_byte = (v1 << 4) | v0

        # Letter nibble
        if info.letter and 'A' <= info.letter <= 'D':
            letter_nibble = 0x0A + (ord(info.letter) - ord('A'))
        else:
            letter_nibble = 0x0F

        # LED nibble
        led_nibble = info.led if info.led in (1, 2, 3, 4) else 0x0F

        payload = bytes([song_h, song_l, verse_byte, letter_nibble, led_nibble])
        self.send(
            protocol.Addr.BASE,
            protocol.MsgType.NOW_PLAYING_INFO,
            payload
        )

    def repeat_verse(self):
        """Skočí na začátek aktuální sloky."""
        if _player and _player.playing:
            _player.repeat_verse()

    def toggle_repeat(self):
        """Přepne režim automatického opakování sloky."""
        if _player and _player.playing:
            enabled = _player.toggle_repeat()
            return enabled
        return False

    def get_current_display_info(self):
        """Vrací aktuální DisplayInfo z přehrávače, nebo None."""
        if _player and _player.playing:
            return _player.current_info
        return None

    def is_playing(self):
        """Vrací True pokud probíhá přehrávání."""
        return _player is not None and _player.playing

    def record_song(self):
        global recorder
        time.sleep(1)
        self.organOn()
        time.sleep(1)
        filename = datetime.now().strftime("%Y-%m-%dT%H:%M:%S.mid")
        inport = midi.get_inport()
        alsa_port = inport.split(" ")[-1] if inport else ""
        recorder = subprocess.Popen(
            ["arecordmidi", "-p", alsa_port,
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


def get_port():
    key = "VID:PID=ABCD:1234"
    comport = None

    for port in serial.tools.list_ports.comports():
        if port.hwid.find(key) > -1:
            comport = port

    if comport is None:
        print("MIDIControl COM port not found!")
    else:
        print("Found MIDIControl COM port: " + str(comport.name))

    return comport
