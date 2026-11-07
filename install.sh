#!/bin/sh
# MIDIControl PC player: one-command Docker installer.
#   curl -fsSL https://raw.githubusercontent.com/WojtaCZ/midicontrol-sw-py/main/install.sh | sh
# Works when piped (never reads stdin, never relies on its own path).
set -eu

usage() {
    cat <<'USAGE'
Usage: install.sh [--tag TAG] [--no-updates] [--uninstall [--purge]]
  --tag TAG       image tag to run (default: stable; "edge" follows main)
  --no-updates    write UPDATE_ENABLED=0 (the updater stays idle)
  --uninstall     stop and remove the containers, compose.yaml, .env and the wrapper;
                  keeps data/ unless --purge is given
  --purge         with --uninstall: also delete data/ (songs, recordings, config)
Existing .env and data/config.conf are never overwritten.
Environment overrides:
  MC_PREFIX        install directory (default /opt/midicontrol)
  MC_BINDIR        where the "midicontrol" wrapper goes (default /usr/local/bin)
  MC_DOCKER        docker command (default docker)
  MC_RAW_URL       where compose.template.yaml and midicontrol-wrapper.sh are fetched
                   (default raw GitHub, WojtaCZ/midicontrol-sw-py main, docker/)
  MC_WAIT_SECONDS  how long to wait for /api/status (default 60, 0 = do not wait)
USAGE
}

die() { echo "install.sh: $*" >&2; exit 1; }

TAG=""
NO_UPDATES=0
UNINSTALL=0
PURGE=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --tag)
            [ "$#" -ge 2 ] || die "--tag needs a value"
            TAG=$2
            shift
            ;;
        --no-updates) NO_UPDATES=1 ;;
        --uninstall) UNINSTALL=1 ;;
        --purge) PURGE=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done
[ "$PURGE" = 0 ] || [ "$UNINSTALL" = 1 ] || die "--purge only makes sense with --uninstall"

DOCKER=${MC_DOCKER:-docker}
PREFIX=${MC_PREFIX:-/opt/midicontrol}
BINDIR=${MC_BINDIR:-/usr/local/bin}
RAW_URL=${MC_RAW_URL:-https://raw.githubusercontent.com/WojtaCZ/midicontrol-sw-py/main/docker}
WAIT=${MC_WAIT_SECONDS:-60}
PORT=50118
# URL of install.sh itself (it lives one level above docker/).
INSTALL_URL="${RAW_URL%/docker}/install.sh"

# sudo only when a real system location is touched by a non-root user.
SUDO=""
if [ -z "${MC_PREFIX:-}" ] || [ -z "${MC_BINDIR:-}" ]; then
    if [ "$(id -u)" != 0 ]; then
        command -v sudo >/dev/null 2>&1 || die "this needs root: run as root or install sudo"
        SUDO=sudo
    fi
fi
OWNER=${SUDO_USER:-$(id -un)}
OWNER_GROUP=$(id -gn "$OWNER" 2>/dev/null || id -gn)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# systemctl --user for the invoking user, also when this script runs under sudo.
user_systemctl() {
    if [ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
        uid=$(id -u "$SUDO_USER")
        sudo -u "$SUDO_USER" env "XDG_RUNTIME_DIR=/run/user/$uid" systemctl --user "$@"
    else
        systemctl --user "$@"
    fi
}

compose() { (cd "$PREFIX" && "$DOCKER" compose "$@"); }

if [ "$UNINSTALL" = 1 ]; then
    command -v "$DOCKER" >/dev/null 2>&1 || die "Docker is not installed, cannot stop the containers."
    if [ -f "$PREFIX/compose.yaml" ]; then
        compose down || die "docker compose down failed; nothing was removed."
    fi
    $SUDO rm -f "$PREFIX/compose.yaml" "$PREFIX/.env" "$BINDIR/midicontrol"
    if [ "$PURGE" = 1 ]; then
        $SUDO rm -rf "$PREFIX/data"
        echo "Removed data/ (songs, recordings, config)."
    else
        echo "Kept $PREFIX/data (songs, recordings, config). Add --purge to delete it."
    fi
    $SUDO rmdir "$PREFIX" 2>/dev/null || true
    echo "MIDIControl uninstalled."
    exit 0
fi

# ---- checks: one plain line per failure -------------------------------------
[ "$(uname -s)" = Linux ] || die "this installer supports Linux hosts only."
command -v "$DOCKER" >/dev/null 2>&1 \
    || die "Docker is not installed. Install it first: https://docs.docker.com/engine/install/"
"$DOCKER" info >/dev/null 2>&1 \
    || die "the Docker daemon is not reachable. Start it (systemctl start docker) and make sure your user may use it (docker group), or run via sudo."
"$DOCKER" compose version >/dev/null 2>&1 \
    || die "the Docker Compose plugin is missing. Install docker-compose-plugin: https://docs.docker.com/compose/install/linux/"
case "$(uname -m)" in
    x86_64|amd64|aarch64|arm64) ;;
    *) die "unsupported architecture $(uname -m): only x86_64 and aarch64 are published." ;;
esac

# Old install: a systemd user unit holds the serial port and port 50118.
if command -v systemctl >/dev/null 2>&1 && user_systemctl is-active --quiet midicontrol.service 2>/dev/null; then
    echo "Found the old systemd user unit midicontrol.service running: stopping and disabling it (the container replaces it)."
    user_systemctl stop midicontrol.service || true
    user_systemctl disable midicontrol.service || true
fi

# Port 50118: free, or held by our own container (re-run).
port_busy() {
    if command -v ss >/dev/null 2>&1; then
        ss -ltn 2>/dev/null | grep -q "[:.]${PORT}[[:space:]]"
    elif command -v netstat >/dev/null 2>&1; then
        netstat -ltn 2>/dev/null | grep -q "[:.]${PORT}[[:space:]]"
    else
        return 1
    fi
}
if port_busy; then
    ours=$("$DOCKER" ps --filter name='^/?midicontrol$' --format '{{.Names}}' 2>/dev/null || true)
    [ -n "$ours" ] || die "port $PORT is already in use by something other than the midicontrol container. Stop that program first (find it with: ss -ltnp | grep $PORT)."
fi

# ---- fetch files ----------------------------------------------------------------
curl -fsSL "$RAW_URL/compose.template.yaml" -o "$tmp/compose.yaml" \
    || die "could not download $RAW_URL/compose.template.yaml"
curl -fsSL "$RAW_URL/midicontrol-wrapper.sh" -o "$tmp/midicontrol" \
    || die "could not download $RAW_URL/midicontrol-wrapper.sh"

# ---- layout -----------------------------------------------------------------------
$SUDO mkdir -p "$PREFIX" "$BINDIR"
$SUDO mkdir -p "$PREFIX/data/songs" "$PREFIX/data/recordings" "$PREFIX/data/diag" "$PREFIX/data/firmware"
# Group-writable and setgid, so songs can be copied in over SSH without sudo.
$SUDO chown -R "$OWNER:$OWNER_GROUP" "$PREFIX/data"
$SUDO chmod 2775 "$PREFIX/data" "$PREFIX/data/songs" "$PREFIX/data/recordings" "$PREFIX/data/diag" "$PREFIX/data/firmware"

$SUDO cp "$tmp/compose.yaml" "$PREFIX/compose.yaml"
$SUDO chmod 644 "$PREFIX/compose.yaml"

if [ -e "$PREFIX/.env" ]; then
    if [ -n "$TAG" ]; then
        echo "Note: --tag ignored, $PREFIX/.env already exists; edit its IMAGE_TAG line instead."
    fi
    if [ "$NO_UPDATES" = 1 ]; then
        echo "Note: --no-updates ignored, $PREFIX/.env already exists; set UPDATE_ENABLED=0 in it instead."
    fi
else
    {
        echo "IMAGE_TAG=${TAG:-stable}"
        echo "UPDATE_WINDOW=02:00-04:00"
        if [ "$NO_UPDATES" = 1 ]; then echo "UPDATE_ENABLED=0"; else echo "UPDATE_ENABLED=1"; fi
    } > "$tmp/env"
    $SUDO cp "$tmp/env" "$PREFIX/.env"
    $SUDO chmod 644 "$PREFIX/.env"
fi

$SUDO cp "$tmp/midicontrol" "$BINDIR/midicontrol"
$SUDO chmod 755 "$BINDIR/midicontrol"

# ---- start -------------------------------------------------------------------------
compose pull || die "docker compose pull failed (no network, or the registry is unreachable)."
compose up -d || die "docker compose up -d failed."

status=""
waited=0
while [ "$waited" -lt "$WAIT" ]; do
    if status=$(curl -fsS --max-time 2 "http://localhost:$PORT/api/status" 2>/dev/null); then
        break
    fi
    status=""
    sleep 1
    waited=$((waited + 1))
done

version=$(printf '%s' "$status" | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')
if [ -z "$version" ]; then
    version=$(sed -n 's/^IMAGE_TAG=//p' "$PREFIX/.env")
    version="${version:-stable} (tag; the status page did not answer yet)"
fi
host=$(hostname 2>/dev/null || true)
[ -n "$host" ] || host=localhost

cat <<DONE

MIDIControl is running.
  Status page: http://$host:$PORT/
  Version:     $version

Commands:
  Console:     midicontrol
  Update now:  docker exec updater midicontrol-updater --once
  Uninstall:   curl -fsSL $INSTALL_URL | sh -s -- --uninstall
DONE
