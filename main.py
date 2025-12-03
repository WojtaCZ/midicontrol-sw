import time
from usb import MIDIControl, LedColor

def main():
    print("Initializing MIDI Control Base driver...")
    device = MIDIControl()
    
    current_state = False
    
    try:
        while True:
            # Check connection
            if device.is_connected():
                try:
                    # 1. Toggle Current Source
                    current_state = not current_state
                    print(f"Setting BASE_CURRENT to {'Enabled' if current_state else 'Disabled'}")
                    device.set_base_current(current_state)
                    
                    # 2. Read back status (Polled automatically by background thread)
                    status = device.get_last_status()
                    print(f"Last Status (from thread): {status}")

                except Exception as e:
                    print(f"Command failed (device might have disconnected): {e}")
            else:
                print("Waiting for device connection...")

            # Run this loop every 2 seconds
            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        device.close()
        print("Driver closed.")

if __name__ == "__main__":
    main()