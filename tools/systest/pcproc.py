"""One isolated virtual `midicontrol` instance: own XDG dirs, config, song/record folders, web port 28130-28139."""
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PC_BIN = os.environ.get("SYSTEST_PC_BIN") or os.path.normpath(os.path.join(HERE, "../../build/midicontrol"))
PORT_RANGE = range(28130, 28140)   # never 28120 (bench PC instance)


def free_port():
    for p in PORT_RANGE:
        with socket.socket() as s:
            try:
                s.bind(("0.0.0.0", p))
                return p
            except OSError:
                continue
    raise RuntimeError("no free web port in 28130-28139")


class PcInstance:
    def __init__(self, tree, midi_key, warmup_ms=200, extra_conf="", songs=()):
        self.tmp = tempfile.mkdtemp(prefix="systest-pc-")
        self.play = os.path.join(self.tmp, "play")
        self.rec = os.path.join(self.tmp, "rec")
        for d in (self.play, self.rec, os.path.join(self.tmp, "run"), os.path.join(self.tmp, "state"),
                  os.path.join(self.tmp, "cfg"), os.path.join(self.tmp, "home")):
            os.makedirs(d, exist_ok=True)
        os.chmod(os.path.join(self.tmp, "run"), 0o700)
        for src, name in songs:
            with open(os.path.join(self.play, name), "wb") as f:
                f.write(src)
        self.port = free_port()
        self.config = os.path.join(self.tmp, "cfg", "config.conf")
        with open(self.config, "w") as f:
            f.write(f"[FILES]\nPlayFolder = {self.play}\nRecordFolder = {self.rec}\n"
                    f"[PLAYBACK]\nWarmupMs = {warmup_ms}\nKeepPowerOn = false\n"
                    f"[WEB]\nEnabled = true\nPort = {self.port}\nBind = 0.0.0.0\n"
                    f"[DIAG]\nDir = {os.path.join(self.tmp, 'state')}\n{extra_conf}\n")
        self.env = dict(os.environ)
        self.env.update(tree.env())
        self.env.update({
            "MIDICONTROL_MIDI_KEY": midi_key,
            "XDG_RUNTIME_DIR": os.path.join(self.tmp, "run"),
            "XDG_STATE_HOME": os.path.join(self.tmp, "state"),
            "XDG_CONFIG_HOME": os.path.join(self.tmp, "cfg"),
            "HOME": os.path.join(self.tmp, "home"),
        })
        self.env.pop("MIDICONTROL_CONFIG", None)
        self.logpath = os.path.join(self.tmp, "pc.log")
        self.proc = None

    def start(self, interactive=False):
        """`interactive`: stdin is a pty (the daemon then shows its prompt); self.tty_master is our end of it."""
        # Safety: a binary without the env seam would open the REAL /dev/ttyACM* (bench hardware).
        with open(PC_BIN, "rb") as f:
            if b"MIDICONTROL_SYSFS_ROOT" not in f.read():
                raise RuntimeError(f"{PC_BIN} lacks the MIDICONTROL_SYSFS_ROOT seam: rebuild it, never run it on real /dev")
        self._log = open(self.logpath, "wb")
        stdin = subprocess.DEVNULL
        args = [PC_BIN, "--config", self.config, "--headless"]
        if interactive:
            import pty
            self.tty_master, slave = pty.openpty()
            stdin = slave
            args = args[:-1]
        self.proc = subprocess.Popen(args, env=self.env, stdout=self._log, stderr=subprocess.STDOUT,
                                     stdin=stdin, start_new_session=interactive)
        if interactive:
            os.close(slave)
        return self

    @property
    def pid(self):
        return self.proc.pid

    def log(self):
        with open(self.logpath, "r", errors="replace") as f:
            return f.read()

    def wait_log(self, text, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in self.log():
                return True
            if self.proc.poll() is not None:
                return text in self.log()
            time.sleep(0.05)
        return False

    def status(self):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/status", timeout=2) as r:
            return json.load(r)

    def open_fds(self):
        out = []
        for fd in os.listdir(f"/proc/{self.pid}/fd"):
            try:
                out.append(os.readlink(f"/proc/{self.pid}/fd/{fd}"))
            except OSError:
                pass
        return out

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(8)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if getattr(self, "_log", None):
            self._log.close()
