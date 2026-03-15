"""
Komunikační protokol MIDIControl — Python implementace.
Zrcadlí C++ knihovnu lib/protocol/.
"""

from enum import IntEnum
from dataclasses import dataclass, field
from typing import Optional


# --- Konstanty ---

FRAME_START = 0x01  # SOH
FRAME_END   = 0x04  # EOT
ESC         = 0x1B

MAX_PAYLOAD  = 128
HEADER_SIZE  = 4
MAX_RETRIES  = 3
RETRY_TIMEOUT = 0.5  # sekundy


# --- Escape tabulka ---

ESCAPE_MAP = {
    0x01: 0x21,  # FRAME_START
    0x04: 0x22,  # FRAME_END
    0x1B: 0x23,  # ESC
    0x24: 0x26,  # $ (RN4870 cmd)
    0x25: 0x27,  # % (RN4870 status)
}

UNESCAPE_MAP = {v: k for k, v in ESCAPE_MAP.items()}


# --- Výčtové typy ---

class Addr(IntEnum):
    PC        = 0x01
    BASE      = 0x02
    REMOTE    = 0x03
    BROADCAST = 0xFF


class MsgType(IntEnum):
    ACK              = 0x01
    NAK              = 0x02
    PLAY_SONG        = 0x10
    STOP_SONG        = 0x11
    RECORD_SONG      = 0x12
    GET_SONG_LIST    = 0x20
    SONG_LIST_RESP   = 0x21
    SIGNAL_PLAYING   = 0x30
    SIGNAL_RECORDING = 0x31
    SIGNAL_STOPPED   = 0x32
    SET_CURRENT      = 0x40
    GET_STATUS       = 0x50
    STATUS_RESP      = 0x51
    KEEPALIVE        = 0x60
    KEEPALIVE_ACK    = 0x61


class NakReason(IntEnum):
    UNKNOWN_MSG     = 0x01
    INVALID_PAYLOAD = 0x02
    CRC_ERROR       = 0x03
    BUSY            = 0x04


# --- Datové struktury ---

@dataclass
class Packet:
    source: int = 0
    dest: int = 0
    packet_id: int = 0
    msg_type: int = 0
    payload: bytes = b''


# --- CRC-8/MAXIM ---

_CRC8_TABLE = [0] * 256

def _init_crc_table():
    poly = 0x31
    for i in range(256):
        crc = i
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
        _CRC8_TABLE[i] = crc

_init_crc_table()


def crc8(data: bytes) -> int:
    crc = 0x00
    for b in data:
        crc = _CRC8_TABLE[crc ^ b]
    return crc


# --- Byte stuffing ---

def _stuff_bytes(data: bytes) -> bytes:
    result = bytearray()
    for b in data:
        if b in ESCAPE_MAP:
            result.append(ESC)
            result.append(ESCAPE_MAP[b])
        else:
            result.append(b)
    return bytes(result)


def _unstuff_bytes(data: bytes) -> Optional[bytes]:
    result = bytearray()
    i = 0
    while i < len(data):
        if data[i] == ESC:
            i += 1
            if i >= len(data):
                return None
            if data[i] not in UNESCAPE_MAP:
                return None
            result.append(UNESCAPE_MAP[data[i]])
        else:
            result.append(data[i])
        i += 1
    return bytes(result)


# --- Kódování a dekódování ---

def encode_packet(pkt: Packet) -> bytes:
    """Zakóduje paket do drátového formátu."""
    length = HEADER_SIZE + len(pkt.payload)
    raw = bytes([
        length,
        pkt.source,
        pkt.dest,
        pkt.packet_id,
        pkt.msg_type,
    ]) + pkt.payload

    checksum = crc8(raw)
    stuffed = _stuff_bytes(raw + bytes([checksum]))

    return bytes([FRAME_START]) + stuffed + bytes([FRAME_END])


def decode_packet(data: bytes) -> Optional[Packet]:
    """Dekóduje rámec (včetně FRAME_START/FRAME_END) na Packet."""
    if len(data) < 2 or data[0] != FRAME_START or data[-1] != FRAME_END:
        return None

    inner = data[1:-1]
    unstuffed = _unstuff_bytes(inner)
    if unstuffed is None:
        return None

    # Minimální délka: Length(1) + Header(4) + CRC(1) = 6
    if len(unstuffed) < 6:
        return None

    length = unstuffed[0]
    if length < HEADER_SIZE:
        return None

    payload_len = length - HEADER_SIZE
    if payload_len > MAX_PAYLOAD:
        return None

    expected_total = 1 + length + 1  # Length byte + content + CRC
    if len(unstuffed) != expected_total:
        return None

    # CRC kontrola
    expected_crc = crc8(unstuffed[:-1])
    if expected_crc != unstuffed[-1]:
        return None

    return Packet(
        source=unstuffed[1],
        dest=unstuffed[2],
        packet_id=unstuffed[3],
        msg_type=unstuffed[4],
        payload=unstuffed[5:5 + payload_len],
    )


# --- Stavový automat pro příjem ---

class PacketParser:
    """Parser pro příjem paketů bajt po bajtu (ISR-safe logika)."""

    def __init__(self):
        self._receiving = False
        self._buffer = bytearray()

    def feed(self, byte: int) -> Optional[Packet]:
        """Přijme jeden bajt. Vrací Packet při kompletním přijetí rámce."""
        if not self._receiving:
            if byte == FRAME_START:
                self._receiving = True
                self._buffer = bytearray()
            return None

        if byte == FRAME_END:
            self._receiving = False
            unstuffed = _unstuff_bytes(bytes(self._buffer))
            if unstuffed is None or len(unstuffed) < 6:
                return None

            length = unstuffed[0]
            if length < HEADER_SIZE:
                return None
            payload_len = length - HEADER_SIZE
            if payload_len > MAX_PAYLOAD:
                return None
            if len(unstuffed) != 1 + length + 1:
                return None

            expected_crc = crc8(unstuffed[:-1])
            if expected_crc != unstuffed[-1]:
                return None

            return Packet(
                source=unstuffed[1],
                dest=unstuffed[2],
                packet_id=unstuffed[3],
                msg_type=unstuffed[4],
                payload=unstuffed[5:5 + payload_len],
            )

        if byte == FRAME_START:
            # Nový rámec — zahodíme rozpracovaný
            self._buffer = bytearray()
            return None

        self._buffer.append(byte)
        return None

    def reset(self):
        self._receiving = False
        self._buffer = bytearray()
