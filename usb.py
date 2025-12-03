import usb1
import struct
import threading
import time
from enum import IntEnum

class Command(IntEnum):
    BASE_STATUS = 0
    BASE_UID = 1
    BASE_CURRENT = 2
    DISPLAY_STATUS = 3
    DISPLAY_SONG = 4
    DISPLAY_VERSE = 5
    DISPLAY_LETTER = 6
    DISPLAY_LED = 7
    UNKNOWN = 8

class LedColor(IntEnum):
    RED = 0x20
    GREEN = 0x22
    BLUE = 0x23
    YELLOW = 0x21
    OFF = 0xE0

class MIDIControl:
    VID = 0xCAFE
    PID = 0x4000
    PRODUCT_STRING = "MIDIControl base"

    def __init__(self):
        self.context = usb1.USBContext()
        self.handle = None
        self._lock = threading.RLock()
        self._running = True
        self._base_status = 0
        
        # Thread for connection management and periodic polling
        self._thread = threading.Thread(target=self._worker_loop, daemon=True)
        self._thread.start()

    def close(self):
        """Stops the worker thread and closes USB context."""
        self._running = False
        if self._thread.is_alive():
            self._thread.join()
        self._disconnect()
        self.context.close()

    def is_connected(self):
        with self._lock:
            return self.handle is not None

    def get_last_status(self):
        """Returns the last status read by the background thread."""
        return self._base_status

    # --------------------------------------------------------------------------
    # Internal Workers and Helpers
    # --------------------------------------------------------------------------

    def _worker_loop(self):
        while self._running:
            with self._lock:
                connected = self.handle is not None

            if not connected:
                # Search for device
                if self._try_connect():
                    print(f"Device connected: {self.PRODUCT_STRING}")
                else:
                    time.sleep(1.0) # Wait before retry
            else:
                # Device is connected, poll status
                try:
                    status_val = self._read_base_status()
                    self._base_status = status_val
                    time.sleep(0.1) # Poll every 100ms
                except usb1.USBError:
                    print("Device disconnected or error reading status.")
                    self._disconnect()

    def _try_connect(self):
        try:
            # Iterate over devices to find matching VID, PID and Product String
            for device in self.context.getDeviceList(skip_on_error=True):
                if device.getVendorID() == self.VID and device.getProductID() == self.PID:
                    try:
                        handle = device.open()
                        if handle.getProduct() == self.PRODUCT_STRING:
                            with self._lock:
                                self.handle = handle
                                # Claim interface if necessary (usually interface 0)
                                self.handle.claimInterface(0)
                            return True
                        else:
                            handle.close()
                    except usb1.USBError:
                        continue
        except Exception as e:
            print(f"Error during connection search: {e}")
        return False

    def _disconnect(self):
        with self._lock:
            if self.handle:
                try:
                    self.handle.releaseInterface(0)
                    self.handle.close()
                except usb1.USBError:
                    pass
                self.handle = None

    def _xfer(self, request_type, request, value, index, length_or_data):
        """
        Generic transfer method guarded by lock.
        request_type: 0xC0 for Read, 0x40 for Write
        """
        with self._lock:
            if self.handle is None:
                raise RuntimeError("Device not connected")
            
            if isinstance(length_or_data, int):
                # Read
                return self.handle.controlRead(request_type, request, value, index, length_or_data)
            else:
                # Write
                return self.handle.controlWrite(request_type, request, value, index, length_or_data)

    # --------------------------------------------------------------------------
    # Command Implementations
    # --------------------------------------------------------------------------

    def _read_base_status(self):
        # BASE_STATUS, get, char
        data = self._xfer(0xC0, Command.BASE_STATUS, 0, 0, 1)
        #return struct.unpack('B', data)[0]

    def get_base_uid(self):
        # BASE_UID, get, 96bit number (12 bytes)
        try:
            data = self._xfer(0xC0, Command.BASE_UID, 0, 0, 12)
            # Unpack as 3 integers (3 * 32 bit = 96 bit)
            parts = struct.unpack('III', data) 
            # Reconstruct: parts[0] is likely LSB or MSB depending on firmware endianness.
            uid = (parts[0] << 64) | (parts[1] << 32) | parts[2]
            return hex(uid)
        except usb1.USBError:
            return None

    def get_base_current(self):
        # BASE_CURRENT, get, char
        data = self._xfer(0xC0, Command.BASE_CURRENT, 0, 0, 1)
        return struct.unpack('B', data)[0] == 1

    def set_base_current(self, enabled: bool):
        # BASE_CURRENT, set, char
        # Value stored in wValue, payload empty
        val = 1 if enabled else 0
        self._xfer(0x40, Command.BASE_CURRENT, val, 0, [0])

    def get_display_status(self):
        # DISPLAY_STATUS, get, char (1=connected, 0=disconnected)
        data = self._xfer(0xC0, Command.DISPLAY_STATUS, 0, 0, 1)
        return struct.unpack('B', data)[0]

    def set_display_song(self, song_number: int, visible: bool):
        # DISPLAY_SONG, uint16, song (0-1999), visibility in MSB
        # This is a 2-byte message, keeping as payload based on "one byte messages" instruction
        if not (0 <= song_number <= 1999):
            raise ValueError("Song number must be 0-1999")
        
        val = song_number
        if visible:
            val |= (1 << 15) # Set MSB (bit 15 for uint16)
            
        self._xfer(0x40, Command.DISPLAY_SONG, 0, 0, struct.pack('<H', val))

    def get_display_song(self):
        data = self._xfer(0xC0, Command.DISPLAY_SONG, 0, 0, 2)
        val = struct.unpack('<H', data)[0]
        visible = bool(val & (1 << 15))
        number = val & 0x7FFF # Mask out MSB
        return number, visible

    def set_display_verse(self, verse_number: int, visible: bool):
        # DISPLAY_VERSE, uint8, number (0-99), visibility in MSB
        # One byte message -> Value in wValue
        if not (0 <= verse_number <= 99):
            raise ValueError("Verse number must be 0-99")
            
        val = verse_number
        if visible:
            val |= (1 << 7) # Set MSB (bit 7 for uint8)
            
        self._xfer(0x40, Command.DISPLAY_VERSE, val, 0, [0])

    def set_display_letter(self, letter: str, visible: bool):
        # DISPLAY_LETTER, uint8, char (A-D), visibility in MSB
        # One byte message -> Value in wValue
        if len(letter) != 1:
            raise ValueError("Letter must be a single character A-D")
        
        char_code = ord(letter.upper())
        val = char_code
        if visible:
            val |= (1 << 7) # Set MSB
            
        self._xfer(0x40, Command.DISPLAY_LETTER, val, 0, [0])

    def set_display_led(self, color: LedColor):
        # DISPLAY_LED, uint8
        # One byte message -> Value in wValue
        self._xfer(0x40, Command.DISPLAY_LED, color.value, 0, [0])