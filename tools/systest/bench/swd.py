#!/usr/bin/env python3
"""Non-halting SWD observation of the base (STM32G431) and the remote (STM32G071) through openocd's TCL port.

Every sample is ONE Tcl script per unit: DHCSR, then the field reads (read_memory = AHB-AP accesses, the core keeps running),
then DHCSR again. 4-byte aligned u32 fields are read with a 32-bit access (no torn values from the ISR).
gdb_port is disabled in the openocd we start, so nothing can halt the core through this path by accident.
Addresses come from `arm-none-eabi-nm -C -S` on the ELFs that are flashed ($SYSTEST_ELFDIR, default
audit/runs/flashed-step3 in the workspace), never hard-coded. Moved here from audit/runs/2026-11-15-be1/tools (BE-1 toolkit,
step-3 C100 copy); additions: guard counters, test key mailbox, frame buffer and current screen of both units.

CLI:
  swd.py sample base|remote|both      one sample as JSON (starts and stops openocd, releases C_DEBUGEN)
  swd.py syms                         print the resolved address map
Library: Swd(unit, logdir).start() / .sample() / .key() / .stop(); used by rig.py.
"""
import threading, json, os, re, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.environ.get("SYSTEST_WS", os.path.abspath(os.path.join(HERE, "..", "..", "..", "..")))
ELFDIR = os.environ.get("SYSTEST_ELFDIR", os.path.join(WS, "audit", "runs", "flashed-step3"))

DHCSR = 0xE000EDF0
DHCSR_RELEASE = 0xA05F0000  # DBGKEY, C_DEBUGEN = 0: the core keeps running, a later fault is not caught by the debugger

UNITS = {
    "base": dict(serial="004200443335510C35383531", cfg=os.path.join(WS, "midicontrol-fw", "openocd.cfg"),
                 elf=os.path.join(ELFDIR, "midicontrol-fw.elf"), tcl=6668, telnet=6667),
    "remote": dict(serial="37FF70064E59313840161343", cfg=os.path.join(WS, "midicontroller-fw", "openocd.cfg"),
                   elf=os.path.join(ELFDIR, "midicontroller-fw.elf"), tcl=6666, telnet=6665),
}

# (field name, symbol (demangled, exact) or "@0xADDR", offset, size, kind) ; kind: u8 u16 u32 hex (byte reads), words
# (32-bit reads, returned as hex of the little-endian bytes; size must be a multiple of 4 and the address aligned)
FIELDS = {
    "remote": [
        ("ticks", "stmcpp::systick::ticks_", 0, 4, "u32"),
        ("autoOffDisabled", "base::Power::autoOffDisabled", 0, 1, "u8"),
        ("rssiPollDisabled", "Bluetooth::rssiPollDisabled", 0, 1, "u8"),
        ("mode", "Bluetooth::(anonymous namespace)::_mode", 0, 1, "u8"),          # 0 DATA? see Mode enum
        ("connected", "Bluetooth::(anonymous namespace)::_connected", 0, 1, "u8"),
        ("bonded", "Bluetooth::(anonymous namespace)::_bonded", 0, 1, "u8"),
        ("moduleUp", "Bluetooth::(anonymous namespace)::moduleUp", 0, 1, "u8"),
        ("rxBytes", "Bluetooth::(anonymous namespace)::rxBytes", 0, 4, "u32"),
        ("lastDataMs", "Bluetooth::(anonymous namespace)::lastDataMs", 0, 4, "u32"),
        ("rxHead", "Bluetooth::(anonymous namespace)::rxHead", 0, 2, "u16"),
        ("rxTail", "Bluetooth::(anonymous namespace)::rxTail", 0, 2, "u16"),
        ("rxOverflows", "Bluetooth::(anonymous namespace)::rxOverflows", 0, 2, "u16"),
        ("txHead", "Bluetooth::(anonymous namespace)::txHead", 0, 2, "u16"),
        ("txTail", "Bluetooth::(anonymous namespace)::txTail", 0, 2, "u16"),
        ("txDmaLen", "Bluetooth::(anonymous namespace)::txDmaLen", 0, 2, "u16"),
        ("inFrame", "Bluetooth::(anonymous namespace)::inFrame", 0, 1, "u8"),
        ("cmdRequested", "Bluetooth::(anonymous namespace)::cmdRequested", 0, 1, "u8"),
        ("endRequested", "Bluetooth::(anonymous namespace)::endRequested", 0, 1, "u8"),
        ("refusals", "Bluetooth::(anonymous namespace)::refusals", 0, 2, "u16"),
        ("pkCount", "Bluetooth::(anonymous namespace)::pkCount", 0, 1, "u8"),
        ("evCount", "Bluetooth::(anonymous namespace)::evCount", 0, 1, "u8"),
        ("stuck", "Bluetooth::(anonymous namespace)::stuck", 0, 8, "hex"),
        ("guard", "Bluetooth::(anonymous namespace)::guard", 0, 0x30, "hex"),
        ("stats", "Bluetooth::(anonymous namespace)::stats", 0, 0x34, "hex"),
        ("ka_wasAlive", "link::(anonymous namespace)::s_wasAlive", 0, 1, "u8"),
        ("ka_seen", "link::(anonymous namespace)::s_keepaliveSeen", 0, 1, "u8"),
        ("ka_last", "link::(anonymous namespace)::s_lastKeepalive", 0, 4, "u32"),
        ("usart2_cr1", "@0x40004400", 0, 4, "u32"),
        ("usart2_isr", "@0x4000441C", 0, 4, "u32"),   # ISR only; never RDR (reading it clears RXNE)
        ("gpiob_odr", "@0x50000414", 0, 4, "u32"),    # PB8 = module RST
        ("gpiob_idr", "@0x50000410", 0, 4, "u32"),
        ("testKeyAck", "mcTestKeyAck", 0, 4, "u32"),
        ("nav", "GUI::nav", 0, 0x28, "words"),
        ("frame", "frameBuffer", 0, 0x41C, "words"),
    ],
    "base": [
        ("ticks", "stmcpp::systick::ticks_", 0, 4, "u32"),
        ("rssiPollDisabled", "Bluetooth::_rssiPollDisabled", 0, 1, "u8"),
        ("mode", "Bluetooth::_mode", 0, 1, "u8"),
        ("connected", "Bluetooth::_connected", 0, 1, "u8"),
        ("streamOpen", "Bluetooth::_streamOpen", 0, 1, "u8"),
        ("peerAccepted", "Bluetooth::_peerAccepted", 0, 1, "u8"),
        ("engineState", "Bluetooth::_engineState", 0, 1, "u8"),
        ("jobCount", "Bluetooth::_jobCount", 0, 1, "u8"),
        ("dollarTimeouts", "Bluetooth::_dollarTimeouts", 0, 1, "u8"),
        ("restartState", "Bluetooth::_restartState", 0, 1, "u8"),
        ("moduleUp", "Bluetooth::_moduleUp", 0, 1, "u8"),
        ("rxBytes", "Bluetooth::_rxBytes", 0, 4, "u32"),
        ("rxRingHead", "Bluetooth::_rxRing", 0x200, 4, "u32"),
        ("rxRingTail", "Bluetooth::_rxRing", 0x204, 4, "u32"),
        ("txRingHead", "Bluetooth::_txRing", 0x400, 4, "u32"),
        ("txRingTail", "Bluetooth::_txRing", 0x404, 4, "u32"),
        ("txDmaLen", "Bluetooth::_txDmaLen", 0, 4, "u32"),
        ("dataRxTs", "Bluetooth::_dataReceivedTimestamp", 0, 4, "u32"),
        ("nextRssiPoll", "Bluetooth::_nextRssiPoll", 0, 4, "u32"),
        ("stats", "_ZN9BluetoothL6_statsE.lto_priv.0", 0, 0x34, "hex"),
        ("lastRemoteRx", "_ZN6routerL13_lastRemoteRxE.lto_priv.0", 0, 4, "u32"),
        ("remoteEverSeen", "_ZN6routerL15_remoteEverSeenE.lto_priv.0", 0, 1, "u8"),
        ("lastKeepalivePc", "router::_lastKeepalive", 0, 4, "u32"),
        ("usart2_isr", "@0x4000441C", 0, 4, "u32"),
        ("gpioa_odr", "@0x48000014", 0, 4, "u32"),    # PA1 = module RST, PA4 = SSR (organ power)
        ("staleKicks", "mcStaleKicks", 0, 4, "u32"),       # step 3: stale-link guard counters (SYS-LINK-01)
        ("staleRestarts", "mcStaleRestarts", 0, 4, "u32"),
        ("resetCause", "resetcause::_last", 0, 4, "u32"),
        ("nav", "_ZN3GUIL3navE.lto_priv.0", 0, 0x28, "words"),
        ("frame", "frameBuffer", 0, 0x41C, "words"),
    ],
}

_symcache = {}


def symbols(unit):
    if unit in _symcache:
        return _symcache[unit]
    out = subprocess.run(["arm-none-eabi-nm", "-C", "-S", UNITS[unit]["elf"]], capture_output=True, text=True, check=True).stdout
    raw = subprocess.run(["arm-none-eabi-nm", "-S", UNITS[unit]["elf"]], capture_output=True, text=True, check=True).stdout
    m = {}
    for txt in (out, raw):
        for line in txt.splitlines():
            p = line.split(None, 3)
            if len(p) == 4:
                m.setdefault(p[3], (int(p[0], 16), int(p[1], 16)))
    _symcache[unit] = m
    return m


def resolve(unit):
    syms = symbols(unit)
    res = []
    for name, sym, off, size, kind in FIELDS[unit]:
        if sym.startswith("@"):
            addr = int(sym[1:], 16)
        else:
            if sym not in syms:
                raise KeyError(f"{unit}: symbol not found in ELF: {sym}")
            base, ssize = syms[sym]
            if off + size > ssize:
                raise ValueError(f"{unit}: {sym} size {ssize} < {off}+{size}")
            addr = base + off
        res.append((name, addr, size, kind))
    return res


def _tcl(port, cmd, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        s.sendall(cmd.encode() + b"\x1a")
        buf = b""
        while not buf.endswith(b"\x1a"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    return buf.rstrip(b"\x1a").decode(errors="replace").strip()


def _filter_log(src, log):
    """Copy openocd output to its log without the per-sample noise: the echoed sample results (lines of 0x words,
    ~4 KB each, ~3 MB/min at fast sampling) and the 'tcl' connection accept/drop pair. A burn-in would otherwise
    write GBs. Dropped lines are counted in a summary line every 10000."""
    dropped, failed = 0, False
    for raw in src:  # never stop draining: a dead reader would fill the pipe and block openocd mid-run
        if failed:
            continue
        try:
            line = raw.decode(errors="replace")
            if line.startswith("0x") or "'tcl' connection" in line:
                dropped += 1
                if dropped % 10000 == 0:
                    log.write(f"[filter] {dropped} sample/tcl lines dropped so far\n"); log.flush()
                continue
            log.write(line); log.flush()
        except Exception:  # e.g. disk full: keep draining, stop logging
            failed = True
    try:
        log.write(f"[filter] eof, {dropped} sample/tcl lines dropped{' (log write failed earlier)' if failed else ''}\n")
        log.close()
    except Exception:
        pass


class Swd:
    def __init__(self, unit, logdir="."):
        self.unit = unit
        self.c = UNITS[unit]
        self.fields = resolve(unit)
        self.proc = None
        self.logdir = logdir
        self.screens = screen_names(unit)
        self.script = self._build_script()

    def _build_script(self):
        parts = [f"[read_memory 0x{DHCSR:08x} 32 1]"]
        for name, addr, size, kind in self.fields:
            if kind == "words":
                assert size % 4 == 0 and addr % 4 == 0, name
                parts.append(f"[read_memory 0x{addr:08x} 32 {size // 4}]")
            elif size == 4 and addr % 4 == 0:
                parts.append(f"[read_memory 0x{addr:08x} 32 1]")
            elif size == 2 and addr % 2 == 0:
                parts.append(f"[read_memory 0x{addr:08x} 16 1]")
            else:
                parts.append(f"[read_memory 0x{addr:08x} 8 {size}]")
        parts.append(f"[read_memory 0x{DHCSR:08x} 32 1]")
        return 'return "' + "|".join(parts) + '"'

    def start(self):
        if self.proc:
            return
        log = open(os.path.join(self.logdir, f"openocd-{self.unit}.log"), "a")
        log.write(f"\n=== start {time.strftime('%F %T')}\n"); log.flush()
        self.proc = subprocess.Popen(
            ["openocd", "-c", f"adapter serial {self.c['serial']}", "-f", self.c["cfg"],
             "-c", "gdb_port disabled", "-c", f"tcl_port {self.c['tcl']}", "-c", f"telnet_port {self.c['telnet']}"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        threading.Thread(target=_filter_log, args=(self.proc.stdout, log), daemon=True).start()
        for _ in range(50):
            time.sleep(0.1)
            if self.proc.poll() is not None:
                raise RuntimeError(f"openocd {self.unit} exited rc={self.proc.returncode} (see openocd-{self.unit}.log)")
            try:
                _tcl(self.c["tcl"], "version", timeout=1)
                return
            except OSError:
                pass
        raise RuntimeError(f"openocd {self.unit} did not open tcl port")

    def tcl(self, cmd):
        return _tcl(self.c["tcl"], cmd)

    def sample(self):
        t0 = time.time(); m0 = time.monotonic()
        out = self.tcl(self.script)
        m1 = time.monotonic()
        parts = out.split("|")
        if len(parts) != len(self.fields) + 2:
            return {"unit": self.unit, "t": t0, "err": out[:300]}
        d = {"unit": self.unit, "t": round(t0, 3), "mono": round((m0 + m1) / 2, 4), "rt_ms": round((m1 - m0) * 1000, 1)}
        d["dhcsr0"] = int(parts[0].split()[0], 0)
        d["dhcsr1"] = int(parts[-1].split()[0], 0)
        d["halted"] = bool(d["dhcsr0"] & (1 << 17)) or bool(d["dhcsr1"] & (1 << 17))
        for (name, addr, size, kind), p in zip(self.fields, parts[1:-1]):
            vals = [int(x, 0) for x in p.split()]
            if kind == "words":
                d[name] = b"".join(v.to_bytes(4, "little") for v in vals).hex()
            elif kind == "hex":
                d[name] = bytes(vals).hex()
            elif len(vals) == 1:
                d[name] = vals[0]
            else:
                d[name] = int.from_bytes(bytes(vals), "little")
        return d

    def read32(self, addr):
        return int(self.tcl(f"read_memory 0x{addr:08x} 32 1").split()[0], 0)

    def write32(self, addr, value):
        return self.tcl(f"write_memory 0x{addr:08x} 32 {{0x{value:08x}}}")

    def stop(self):
        if not self.proc:
            return
        try:
            self.write32(DHCSR, DHCSR_RELEASE)
            _tcl(self.c["tcl"], "shutdown", timeout=2)
        except Exception:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
        self.proc = None


def screen_names(unit):
    """Address -> screen object name (data symbols whose name contains 'Screen'), for decoding nav.stack_."""
    m = {}
    for name, (addr, size) in symbols(unit).items():
        n = re.sub(r"^_ZN3GUIL\d+(\w+)E\.lto_priv\.\d+$", r"GUI::\1", name)
        if "Screen" in n and size >= 8 and addr >= 0x20000000 and "(" not in n:
            m.setdefault(addr, n.replace("GUI::", ""))
    return m


def current_screen(sample, screens):
    """Name of the screen on top of the navigator stack in a sample (nav: theme_, stack_[8], depth_)."""
    nav = sample.get("nav")
    if not nav:
        return None
    b = bytes.fromhex(nav)
    words = [int.from_bytes(b[i:i + 4], "little") for i in range(0, len(b), 4)]
    depth = words[9]
    if not 0 < depth <= 8:
        return f"?depth={depth}"
    p = words[depth]  # stack_[depth-1] = words[1 + depth - 1]
    return screens.get(p, f"?0x{p:08x}")


def main():
    if len(sys.argv) < 2:
        print(__doc__); return 2
    if sys.argv[1] == "syms":
        for u in UNITS:
            for name, addr, size, kind in resolve(u):
                print(f"{u:6} {name:18} 0x{addr:08x} {size:3} {kind}")
        return 0
    if sys.argv[1] == "sample":
        units = ["base", "remote"] if sys.argv[2] == "both" else [sys.argv[2]]
        for u in units:
            s = Swd(u)
            try:
                s.start()
                print(json.dumps(s.sample()))
            finally:
                s.stop()
        return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main())
