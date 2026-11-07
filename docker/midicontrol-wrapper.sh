#!/bin/sh
# Host wrapper installed as /usr/local/bin/midicontrol: runs the terminal client
# inside the player container. Example:
#   midicontrol --cmd "update base /data/firmware/fw-update.bin"
docker=${MC_DOCKER:-docker}
if [ -t 0 ]; then
    exec "$docker" exec -it midicontrol midicontrol "$@"
fi
exec "$docker" exec -i midicontrol midicontrol "$@"
