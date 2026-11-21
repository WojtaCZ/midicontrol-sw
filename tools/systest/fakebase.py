"""Fake MIDIcontrol base on a pty + a fake sysfs/dev tree (virtual tier).

The real `midicontrol` binary is pointed at the tree with MIDICONTROL_SYSFS_ROOT / MIDICONTROL_DEV_ROOT, so it
discovers `ttyACM0` as ABCD:1234 interface 00 and talks to the pty. The protocol codec is the firmware repo's
`proto.py`, imported by path (SYSTEST_BASETEST)."""
import importlib.util
import os
import pty
import select
import shutil
import tempfile
import threading
import time
import tty

MAIN_BASETEST = "/workspaces/MIDICONTROL-b81bba4a/midicontrol-fw/tools/basetest"
HERE = os.path.dirname(os.path.abspath(__file__))


def basetest_dir():
    return os.environ.get("SYSTEST_BASETEST") or os.path.normpath(os.path.join(HERE, "../../../midicontrol-fw/tools/basetest"))


def load_proto():
    for d in (basetest_dir(), MAIN_BASETEST):
        p = os.path.join(d, "proto.py")
        if os.path.exists(p):
            spec = importlib.util.spec_from_file_location("systest_proto", p)
            m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(m)
            return m
    raise ImportError("proto.py not found (set SYSTEST_BASETEST to midicontrol-fw/tools/basetest)")


proto = load_proto()
MT, NAK = proto.MT, proto.NAK
PC, BASE, REMOTE = proto.ADDR_PC, proto.ADDR_BASE, proto.ADDR_REMOTE
GET_INFO = 0x52


class FakeTree:
    """tmp/sys (class/tty/ttyACM0 -> usb device abcd:<pid>) and tmp/dev/ttyACM0 -> pty slave."""

    def __init__(self, root=None, usb_pid="1234"):
        self.root = root or tempfile.mkdtemp(prefix="systest-tree-")
        self.sys = os.path.join(self.root, "sys")
        self.dev = os.path.join(self.root, "dev")
        os.makedirs(self.dev)
        self.usb_pid = usb_pid
        self.nodes = {}

    def _w(self, path, text):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text + "\n")

    def add_tty(self, name, slave_path, ifnum="00", iface="", vid="abcd", pid=None, dev="1-1"):
        pid = pid or self.usb_pid
        devdir = os.path.join(self.sys, "devices/usb1", dev)
        self._w(os.path.join(devdir, "idVendor"), vid)
        self._w(os.path.join(devdir, "idProduct"), pid)
        ifdir = os.path.join(devdir, f"{dev}:1.{int(ifnum)}")
        self._w(os.path.join(ifdir, "bInterfaceNumber"), ifnum)
        if iface:
            self._w(os.path.join(ifdir, "interface"), iface)
        tdir = os.path.join(self.sys, "class/tty", name)
        os.makedirs(tdir, exist_ok=True)
        link = os.path.join(tdir, "device")
        if os.path.lexists(link):
            os.unlink(link)
        os.symlink(ifdir, link)
        node = os.path.join(self.dev, name)
        if os.path.lexists(node):
            os.unlink(node)
        os.symlink(slave_path, node)
        self.nodes[name] = (tdir, devdir)

    def remove_tty(self, name):
        tdir, devdir = self.nodes.pop(name)
        shutil.rmtree(tdir, ignore_errors=True)
        shutil.rmtree(devdir, ignore_errors=True)
        try:
            os.unlink(os.path.join(self.dev, name))
        except FileNotFoundError:
            pass

    def env(self):
        return {"MIDICONTROL_SYSFS_ROOT": self.sys, "MIDICONTROL_DEV_ROOT": self.dev}

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)


class FakeBase:
    """Answers like the base firmware. All knobs are plain attributes, safe to flip from the test thread."""

    def __init__(self, tree: FakeTree, name="ttyACM0"):
        self.tree, self.name = tree, name
        self.rx = []           # (monotonic, Packet) every valid frame from the PC
        self.rx_raw_bytes = 0
        self.ssr = False
        self.ble = False
        self.remote_mode = "ack"       # ack | unreachable | drop
        self.silent = False            # answer nothing at all (link loss from the PC's view)
        self.answer_keepalive = True
        self.drop_types = {}           # MT value -> number of upcoming frames to drop (not executed, no ACK)
        self.corrupt_next = 0          # corrupt the CRC of the next N frames we send
        self.tx = []                   # (monotonic, Packet) frames sent to the PC
        self._id = 1
        self._last_pid = None
        self._lock = threading.Lock()
        self._master = self._slave = None
        self._run = False
        self._thr = None
        self.parser = proto.FrameParser()
        self.stray = b""

    # lifecycle -------------------------------------------------------------------------------
    def start(self):
        self._master, self._slave = pty.openpty()
        tty.setraw(self._slave)
        slave_path = os.ttyname(self._slave)
        self.tree.add_tty(self.name, slave_path)
        self._run = True
        self._thr = threading.Thread(target=self._loop, daemon=True)
        self._thr.start()
        return self

    def vanish(self):
        """USB unplug: the node and the sysfs entry disappear, the pty is closed."""
        self._run = False
        if self._thr:
            self._thr.join(2)
        self.tree.remove_tty(self.name)
        for fd in (self._master, self._slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self._master = self._slave = None
        self.parser = proto.FrameParser()
        self._last_pid = None

    def restore(self):
        self.start()

    def stop(self):
        if self._master is not None:
            self.vanish()

    # io --------------------------------------------------------------------------------------
    def send(self, src, dst, mtype, payload=b"", pid=None):
        if pid is None:
            pid = self._id
            self._id = (self._id + 1) & 0xFF
        data = proto.encode(src, dst, pid, int(mtype), payload)
        if self.corrupt_next > 0:
            self.corrupt_next -= 1
            data = data[:-2] + bytes([data[-2] ^ 0xFF]) + data[-1:]
        self.send_raw(data)
        self.tx.append((time.monotonic(), proto.Packet(src, dst, pid, int(mtype), bytes(payload))))
        return pid

    def send_raw(self, data: bytes):
        if self._master is not None:
            try:
                os.write(self._master, data)
            except OSError:
                pass

    def _ack(self, p):
        self.send(BASE, PC, MT.ACK, bytes([p.pid]))

    def _loop(self):
        while self._run:
            try:
                r, _, _ = select.select([self._master], [], [], 0.05)
                if not r:
                    continue
                data = os.read(self._master, 512)
            except OSError:
                return
            self.rx_raw_bytes += len(data)
            for p in self.parser.feed(data):
                self.rx.append((time.monotonic(), p))
                if not self.silent:
                    self._handle(p)
            if self.parser.text:
                self.stray += self.parser.text
                self.parser.text = b""

    def _handle(self, p):
        if p.mtype == MT.KEEPALIVE:
            if self.answer_keepalive:
                self.send(BASE, PC, MT.KEEPALIVE_ACK)
            return
        if p.mtype in (MT.ACK, MT.NAK, MT.KEEPALIVE_ACK):
            return
        n = self.drop_types.get(p.mtype, 0)
        if n > 0:
            self.drop_types[p.mtype] = n - 1
            return
        if p.dst == REMOTE:
            if self.remote_mode == "ack":
                self.send(REMOTE, PC, MT.ACK, bytes([p.pid]))
            elif self.remote_mode == "unreachable":
                self.send(BASE, PC, MT.NAK, bytes([p.pid, NAK.UNREACHABLE]))
            return
        if self._last_pid == p.pid:      # router dedup: one last PacketId per source
            self._ack(p)
            return
        self._last_pid = p.pid
        if p.mtype == MT.SET_CURRENT and p.payload:
            self.ssr = p.payload[0] != 0
        self._ack(p)
        if p.mtype == MT.GET_STATUS:
            self.send(BASE, PC, MT.STATUS_RESP, bytes([int(self.ble), int(self.ssr), 1, 1]))
        elif p.mtype == MT.DIAG_GET:
            self.send(BASE, PC, MT.DIAG_RESP, proto.diag_encode_resp(1, 0, []))

    # remote-side commands, as the remote would relay them via the base -------------------------
    def remote_cmd(self, mtype, payload=b""):
        return self.send(REMOTE, PC, mtype, payload)

    def play(self, code):
        return self.remote_cmd(MT.PLAY_SONG, code.encode())

    def stop_cmd(self):
        return self.remote_cmd(MT.STOP_SONG)

    # observation ------------------------------------------------------------------------------
    def received(self, mtype=None, dst=None):
        return [(t, p) for t, p in list(self.rx)
                if (mtype is None or p.mtype == mtype) and (dst is None or p.dst == dst)]

    def wait_for(self, pred, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for t, p in list(self.rx):
                if pred(p):
                    return t, p
            time.sleep(0.02)
        return None
