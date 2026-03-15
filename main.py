import midi
import comm
import config
import protocol

# Načtení konfigurace
config.load()

# Inicializace MIDI portů
midi_in, midi_out = midi.get_ports()

# Vytvoření komunikačního handleru
handler = comm.CommHandler()


def on_packet(pkt):
    """Callback pro příchozí pakety — výpis do konzole."""
    msg = pkt.msg_type

    if msg == protocol.MsgType.KEEPALIVE_ACK:
        return  # Tiše zpracováno v comm._handle_internal()

    if msg == protocol.MsgType.ACK:
        if len(pkt.payload) >= 1:
            print(f"  <- ACK pro PacketID={pkt.payload[0]:#04x}")

    elif msg == protocol.MsgType.NAK:
        if len(pkt.payload) >= 2:
            print(f"  <- NAK pro PacketID={pkt.payload[0]:#04x}, důvod={pkt.payload[1]:#04x}")

    elif msg == protocol.MsgType.SIGNAL_PLAYING:
        song = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"  <- Přehrávání: {song}")

    elif msg == protocol.MsgType.SIGNAL_RECORDING:
        filename = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"  <- Nahrávání: {filename}")

    elif msg == protocol.MsgType.SIGNAL_STOPPED:
        print("  <- Zastaveno")

    elif msg == protocol.MsgType.SONG_LIST_RESP:
        song_list = pkt.payload.rstrip(b'\x00').decode('utf-8', errors='replace')
        songs = song_list.split(';')
        print(f"  <- Seznam písní: {songs}")

    elif msg == protocol.MsgType.STATUS_RESP:
        if len(pkt.payload) >= 3:
            ble = bool(pkt.payload[0])
            current = bool(pkt.payload[1])
            display = bool(pkt.payload[2])
            pc_alive = bool(pkt.payload[3]) if len(pkt.payload) >= 4 else False
            print(f"  <- Status: BLE={ble}, proud={current}, displej={display}, pc_alive={pc_alive}")

    elif msg == protocol.MsgType.STOP_SONG:
        # Base poslal STOP_SONG (uživatel stiskl encoder)
        print("  <- Base zastavil přehrávání")
        handler.stop_song()


handler.on_packet(on_packet)

# Připojení k zařízení
if not handler.connect():
    print("Nelze se připojit k MIDIControl!")
    exit(1)

print()
print("MIDIControl — interaktivní ovládání")
print("Příkazy: play <píseň>, stop, record, list, on, off, status,")
print("         repeat, repeatmode, quit")
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
            print(f"Přehrávání: {song}")
            handler.play_song(song)

        elif command == "stop":
            print("Zastavuji...")
            handler.stop_song()

        elif command == "record":
            print("Nahrávání...")
            handler.record_song()

        elif command == "list":
            print("Dotaz na seznam písní...")
            handler.get_song_list()

        elif command == "on":
            print("Zapínám proud...")
            handler.organOn()

        elif command == "off":
            print("Vypínám proud...")
            handler.organOff()

        elif command == "repeat":
            if handler.is_playing():
                handler.repeat_verse()
                print("Skok na začátek aktuální sloky")
            else:
                print("Nic se nepřehrává")

        elif command == "repeatmode":
            if handler.is_playing():
                enabled = handler.toggle_repeat()
                print(f"Opakování sloky: {'zapnuto' if enabled else 'vypnuto'}")
            else:
                print("Nic se nepřehrává")

        elif command == "status":
            print(f"base_alive={handler.is_base_alive()}")
            print(f"playing={handler.is_playing()}")
            info = handler.get_current_display_info()
            if info:
                letter = info.letter if info.letter else '-'
                print(f"  song={info.song}, verse={info.verse}, letter={letter}, led={info.led}")
            print("Dotaz na status...")
            handler.send_with_ack(
                protocol.Addr.BASE,
                protocol.MsgType.GET_STATUS
            )

        elif command == "quit" or command == "exit":
            break

        else:
            print(f"Neznámý příkaz: {cmd}")
            print("Příkazy: play <píseň>, stop, record, list, on, off, status,")
            print("         repeat, repeatmode, quit")

except KeyboardInterrupt:
    print("\nUkončuji...")

handler.disconnect()
print("Hotovo.")
