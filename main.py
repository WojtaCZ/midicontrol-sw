import os
import midi
import comm
import config
import protocol

# Load configuration
config.load()

# Initialize MIDI ports
midi_in, midi_out = midi.get_ports()

# Create communication handler
handler = comm.CommHandler()


def on_packet(pkt):
    """Callback for incoming packets — prints to console."""
    msg = pkt.msg_type

    if msg == protocol.MsgType.KEEPALIVE_ACK:
        return  # Silently handled in comm._handle_internal()

    if msg == protocol.MsgType.ACK:
        if len(pkt.payload) >= 1:
            print(f"  <- ACK for PacketID={pkt.payload[0]:#04x}")

    elif msg == protocol.MsgType.NAK:
        if len(pkt.payload) >= 2:
            print(f"  <- NAK for PacketID={pkt.payload[0]:#04x}, reason={pkt.payload[1]:#04x}")

    elif msg == protocol.MsgType.SIGNAL_PLAYING:
        song = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"  <- Playing: {song}")

    elif msg == protocol.MsgType.SIGNAL_RECORDING:
        filename = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"  <- Recording: {filename}")

    elif msg == protocol.MsgType.SIGNAL_STOPPED:
        print("  <- Stopped")

    elif msg == protocol.MsgType.SONG_LIST_RESP:
        if len(pkt.payload) > 1:
            has_more = pkt.payload[0]
            song_list = pkt.payload[1:].rstrip(b'\x00').decode('utf-8', errors='replace')
            songs = song_list.split(';')
            print(f"  <- Song list (has_more={has_more}): {songs}")
        else:
            print(f"  <- Song list: (empty)")

    elif msg == protocol.MsgType.STATUS_RESP:
        if len(pkt.payload) >= 3:
            ble = bool(pkt.payload[0])
            current = bool(pkt.payload[1])
            display = bool(pkt.payload[2])
            pc_alive = bool(pkt.payload[3]) if len(pkt.payload) >= 4 else False
            print(f"  <- Status: BLE={ble}, current={current}, display={display}, pc_alive={pc_alive}")

    elif msg == protocol.MsgType.GET_SONG_LIST:
        # Base požaduje seznam písní (s offsetem pro paginaci)
        try:
            offset = pkt.payload[0] if len(pkt.payload) >= 1 else 0
            folder = config.playFolder
            files = sorted([
                os.path.splitext(f)[0]
                for f in os.listdir(folder)
                if f.lower().endswith('.mid')
            ])
            files = files[offset:]  # přeskočit již odeslané písně
            # Sestavení odpovědi: [has_more, song1;song2;...]
            result = bytearray()
            result.append(0x00)  # placeholder pro has_more
            count = 0
            for f in files:
                entry = f.encode('utf-8')
                needed = len(entry) + (1 if count > 0 else 0)  # +1 pro středník
                if len(result) + needed > 128:
                    result[0] = 0x01  # has_more = true
                    break
                if count > 0:
                    result.append(0x3B)  # středník
                result.extend(entry)
                count += 1
            print(f"  <- Song list request (offset={offset}), sending {count} songs, has_more={result[0]}")
            handler.send_with_ack(
                protocol.Addr.BASE,
                protocol.MsgType.SONG_LIST_RESP,
                bytes(result)
            )
        except Exception as e:
            print(f"  <- Error reading song folder: {e}")

    elif msg == protocol.MsgType.PLAY_SONG:
        # Base poslal PLAY_SONG (uživatel vybral píseň z OLED menu)
        song_name = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"  <- Base requested play: {song_name}")
        handler.play_song(song_name)

    elif msg == protocol.MsgType.STOP_SONG:
        # Base sent STOP_SONG (user pressed encoder)
        print("  <- Base stopped playback")
        handler.stop_song()


handler.on_packet(on_packet)

# Connect to device
if not handler.connect():
    print("Cannot connect to MIDIControl!")
    exit(1)

print()
print("MIDIControl — interactive control")
print("Commands: play <song>, stop, record, list, on, off, status,")
print("          repeat, repeatmode, quit")
print()

try:
    while True:
        try:
            cmd = input("> ").strip()
        except EOFError:
            break

        if not cmd:
            continue

        parts = cmd.split(maxsplit=1)
        command = parts[0].lower()

        if command == "play" and len(parts) > 1:
            song = parts[1]
            print(f"Playing: {song}")
            handler.play_song(song)

        elif command == "stop":
            print("Stopping...")
            handler.stop_song()

        elif command == "record":
            print("Recording...")
            handler.record_song()

        elif command == "list":
            print("Requesting song list...")
            handler.get_song_list()

        elif command == "on":
            print("Turning on power...")
            handler.organOn()

        elif command == "off":
            print("Turning off power...")
            handler.organOff()

        elif command == "repeat":
            if handler.is_playing():
                handler.repeat_verse()
                print("Jumping to start of current verse")
            else:
                print("Nothing is playing")

        elif command == "repeatmode":
            if handler.is_playing():
                enabled = handler.toggle_repeat()
                print(f"Verse repeat: {'on' if enabled else 'off'}")
            else:
                print("Nothing is playing")

        elif command == "status":
            print(f"base_alive={handler.is_base_alive()}")
            print(f"playing={handler.is_playing()}")
            info = handler.get_current_display_info()
            if info:
                letter = info.letter if info.letter else '-'
                print(f"  song={info.song}, verse={info.verse}, letter={letter}, led={info.led}")
            print("Requesting status...")
            handler.send_with_ack(
                protocol.Addr.BASE,
                protocol.MsgType.GET_STATUS
            )

        elif command == "quit" or command == "exit":
            break

        else:
            print(f"Unknown command: {cmd}")
            print("Commands: play <song>, stop, record, list, on, off, status,")
            print("          repeat, repeatmode, quit")

except KeyboardInterrupt:
    print("\nExiting...")

handler.disconnect()
print("Done.")
