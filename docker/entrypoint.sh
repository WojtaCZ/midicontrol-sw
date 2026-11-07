#!/bin/sh
# Container entrypoint: seed /data on first start, then exec the command.
# No arguments = run the player; any other arguments replace the default command
# (e.g. "midicontrol-updater" for the updater service).
set -e
umask 0002

DATA_DIR="${MC_DATA_DIR:-/data}"
BIN="${MC_BIN:-midicontrol}"
SEED="${MC_SEED_CONFIG:-/usr/local/share/midicontrol/config.conf}"

mkdir -p "$DATA_DIR/songs" "$DATA_DIR/recordings" "$DATA_DIR/diag" "$DATA_DIR/firmware"
if [ ! -f "$DATA_DIR/config.conf" ]; then
    cp "$SEED" "$DATA_DIR/config.conf"
fi

if [ "$#" -eq 0 ]; then
    set -- "$BIN" --headless --exit-on-stall --config "$DATA_DIR/config.conf"
fi
exec "$@"
