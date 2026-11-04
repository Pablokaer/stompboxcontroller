#!/usr/bin/env python3
"""stompboxd – footswitch controller for the JME STOMPBOX (BLE-MIDI).

Finds and keeps the pedal connected over Bluetooth, reads footswitches on the
Pi's GPIO, turns press / hold / multi-button combos into MIDI commands for the
pedal, and serves the settings page on port 80.

Files:   /etc/stompbox/config.json   button settings (edited through the page)
         /etc/stompbox/pedal.json    the pedal this Pi is locked to (found automatically)
         /etc/stompbox/hotspot.conf  hotspot name / password / country
Web:     http://192.168.4.1  (hotspot)  or  http://<pi-ip>  (any network the Pi joins)
"""
import json, os, re, socket, subprocess, threading, time, shutil
from collections import deque
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

VERSION = "1.1.0"
ETC = os.environ.get("STOMPBOX_ETC", "/etc/stompbox")
CONFIG_PATH = os.environ.get("STOMPBOX_CONFIG", os.path.join(ETC, "config.json"))
PEDAL_PATH = os.path.join(ETC, "pedal.json")
HOTSPOT_PATH = os.path.join(ETC, "hotspot.conf")
WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
HTTP_PORT = int(os.environ.get("STOMPBOX_PORT", "80"))
PEDAL_NAME = "JME STOMPBOX"                       # Bluetooth name the pedal advertises
MIDI_UUID = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"  # standard BLE-MIDI service
MIDI_CH = 0  # channel 1

# effect -> (CC number, default "on" value). Low bits = effect type, bit 64 = on.
EFFECTS = {"mod": (3, 64), "delay": (4, 64), "reverb": (5, 65), "gate": (6, 64), "eq": (7, 64)}
EFFECT_NAMES = {"mod": "MOD", "delay": "DELAY", "reverb": "REVERB", "gate": "GATE", "eq": "EQ"}
ACTION_TYPES = {"none", "toggle", "momentary", "on", "off", "all_off", "all_on",
                "preset_next", "preset_prev", "preset"}
GPIO_PHYS = {4: 7, 17: 11, 27: 13, 22: 15, 23: 16, 24: 18, 25: 22, 5: 29, 6: 31,
             12: 32, 13: 33, 19: 35, 16: 36, 26: 37, 20: 38, 21: 40}


def a(t, effect=None, preset=None):
    d = {"type": t}
    if effect: d["effect"] = effect
    if preset is not None: d["preset"] = preset
    return d


def default_config():
    def btn(gpio, eff):
        return {"gpio": gpio, "press": a("toggle", eff), "hold": a("none")}
    return {
        "active_profile": "4",
        "hold_ms": 600,
        "combo_ms": 120,
        "profiles": {
            "4": {"buttons": [btn(17, "mod"), btn(27, "delay"), btn(22, "reverb"), btn(23, "eq")],
                  "combos": []},
            "5": {"buttons": [btn(17, "mod"), btn(27, "delay"), btn(22, "reverb"), btn(23, "eq"),
                              btn(24, "gate")],
                  "combos": []},
        },
    }


# ----------------------------------------------------------------- validation
def validate_action(act, allow_momentary=True):
    if not isinstance(act, dict) or act.get("type") not in ACTION_TYPES:
        raise ValueError(f"unknown action {act!r}")
    t = act["type"]
    if t == "momentary" and not allow_momentary:
        raise ValueError("momentary is only allowed as a press action")
    if t in ("toggle", "momentary", "on", "off"):
        if act.get("effect") not in EFFECTS:
            raise ValueError(f"action {t} needs an effect")
        return {"type": t, "effect": act["effect"]}
    if t == "preset":
        p = int(act.get("preset", -1))
        if not 0 <= p <= 127:
            raise ValueError("preset must be 0-127")
        return {"type": t, "preset": p}
    return {"type": t}


def validate_config(cfg):
    out = {"active_profile": str(cfg.get("active_profile", "4")),
           "hold_ms": int(cfg.get("hold_ms", 600)),
           "combo_ms": int(cfg.get("combo_ms", 120)),
           "profiles": {}}
    if out["active_profile"] not in ("4", "5"):
        raise ValueError("active_profile must be 4 or 5")
    if not 200 <= out["hold_ms"] <= 3000:
        raise ValueError("hold time must be 200-3000 ms")
    if not 30 <= out["combo_ms"] <= 500:
        raise ValueError("combo window must be 30-500 ms")
    for name, n in (("4", 4), ("5", 5)):
        prof = cfg["profiles"][name]
        buttons = prof["buttons"]
        if len(buttons) != n:
            raise ValueError(f"profile {name} needs {n} buttons")
        vb, pins = [], set()
        for i, b in enumerate(buttons):
            g = int(b["gpio"])
            if g not in GPIO_PHYS:
                raise ValueError(f"GPIO{g} is not allowed")
            if g in pins:
                raise ValueError(f"profile {name}: GPIO{g} used twice")
            pins.add(g)
            press = validate_action(b.get("press", a("none")))
            hold = validate_action(b.get("hold", a("none")), allow_momentary=False)
            if press["type"] == "momentary" and hold["type"] != "none":
                raise ValueError(f"button {i+1}: a momentary button cannot also have a hold action")
            vb.append({"gpio": g, "press": press, "hold": hold})
        vc, seen = [], set()
        for c in prof.get("combos", []):
            bs = sorted({int(x) for x in c["buttons"]})
            if len(bs) < 2 or any(not 0 <= x < n for x in bs):
                raise ValueError("a combo needs 2 or more valid buttons")
            if tuple(bs) in seen:
                raise ValueError(f"combo {[x+1 for x in bs]} is defined twice")
            seen.add(tuple(bs))
            vc.append({"buttons": bs, "action": validate_action(c["action"], allow_momentary=False)})
        out["profiles"][name] = {"buttons": vb, "combos": vc}
    return out


def load_config():
    try:
        with open(CONFIG_PATH) as f:
            return validate_config(json.load(f))
    except FileNotFoundError:
        cfg = default_config()
        save_config(cfg)
        return cfg


def save_config(cfg):
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    tmp = CONFIG_PATH + ".tmp"
    with open(tmp, "w") as f:
        json.dump(cfg, f, indent=2)
    os.replace(tmp, CONFIG_PATH)


# ----------------------------------------------------------------------- log
LOG = deque(maxlen=40)
LOG_LOCK = threading.Lock()


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    with LOG_LOCK:
        LOG.appendleft(line)


# --------------------------------------------------------------------- pedal
def run_cmd(args, timeout=30):
    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout or "") + (r.stderr or "")
    except subprocess.TimeoutExpired:
        return 124, "timed out"
    except FileNotFoundError as e:
        return 127, str(e)


MAC_RE = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")


def parse_pedal_file(d):
    """pedal.json -> (devices, startup_mac). Also reads the 1.0 format {"mac": ...}."""
    devices = []
    if isinstance(d.get("devices"), list):
        for x in d["devices"]:
            mac = str(x.get("mac", "")).upper()
            if MAC_RE.match(mac) and mac not in [y["mac"] for y in devices]:
                devices.append({"mac": mac, "name": x.get("name") or mac, "added": x.get("added", "")})
        default = str(d.get("default") or "").upper() or None
        if default not in [y["mac"] for y in devices]:
            default = devices[0]["mac"] if devices else None
        return devices, default
    mac = str(d.get("mac", "")).upper()
    if MAC_RE.match(mac):
        return [{"mac": mac, "name": d.get("name") or PEDAL_NAME, "added": d.get("paired", "")}], mac
    return [], None


class Pedal:
    """Bluetooth MIDI devices: which one to use, and keeping it connected.

    pedal.json holds the saved devices and the *startup device* – the one the Pi
    connects to at every start and keeps reconnecting to. "Connect now" switches
    to another device right away; it only becomes the startup device when asked.
    With no device saved at all, the Pi looks for a JME STOMPBOX by itself and
    saves it as the startup device.
    Devices are identified by their hardware address, so another device with the
    same name is never used by accident.
    """

    def __init__(self, start=True):
        self.devices, self.default = [], None
        self.state = "starting"
        self.connected = False
        self.lock = threading.RLock()
        self.bt_lock = threading.Lock()      # one Bluetooth scan at a time
        self.wake = threading.Event()        # interrupts waits when the user changes something
        self.misses = 0
        try:
            with open(PEDAL_PATH) as f:
                self.devices, self.default = parse_pedal_file(json.load(f))
        except (OSError, ValueError):
            pass
        self.target = self.default           # device kept connected right now
        if start:
            threading.Thread(target=self._loop, daemon=True).start()

    # ------------------------------------------------------------- storage
    def _save(self):
        with self.lock:
            data = {"default": self.default, "devices": self.devices}
        os.makedirs(ETC, exist_ok=True)
        tmp = PEDAL_PATH + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f, indent=2)
        os.replace(tmp, PEDAL_PATH)

    def name_of(self, mac):
        for d in self.devices:
            if d["mac"] == mac:
                return d["name"]
        return None

    def target_name(self):
        """Name of the device in use – BlueZ names its ALSA MIDI port after it."""
        return self.name_of(self.target) or PEDAL_NAME

    # ------------------------------------------------------------ bluetooth
    @staticmethod
    def _bt_power():
        for r in __import__("glob").glob("/sys/class/rfkill/rfkill*"):
            try:
                if open(os.path.join(r, "type")).read().strip() == "bluetooth":
                    with open(os.path.join(r, "soft"), "w") as f:
                        f.write("0")
            except OSError:
                pass
        rc, out = run_cmd(["bluetoothctl", "show"], 10)
        if "Powered: yes" not in out:
            run_cmd(["bluetoothctl", "power", "on"], 10)
            return False
        return True

    @staticmethod
    def _info(mac):
        rc, out = run_cmd(["bluetoothctl", "info", mac], 10)
        name = re.search(r"^\s*Name: (.+)$", out, re.M) or re.search(r"^\s*Alias: (.+)$", out, re.M)
        rssi = re.search(r"RSSI: .*?\((-?\d+)\)", out)
        return {"mac": mac, "name": name.group(1).strip() if name else mac,
                "rssi": int(rssi.group(1)) if rssi else None,
                "midi": MIDI_UUID in out.lower(),
                "connected": "Connected: yes" in out}

    def _is_connected(self, mac):
        return self._info(mac)["connected"]

    def _scan(self, secs=8):
        """Bluetooth discovery. Returns {address: signal dBm or None} for every device
        seen during this scan (signal strength is only reported while scanning)."""
        with self.bt_lock:
            rc, out = run_cmd(["bluetoothctl", "--timeout", str(secs), "scan", "on"], secs + 10)
        seen = {}
        for line in re.sub(r"\x1b\[[0-9;]*m", "", out).splitlines():
            m = re.search(r"\[(NEW|CHG)\] Device ([0-9A-Fa-f:]{17})(.*)", line)
            if not m:
                continue
            mac = m.group(2).upper()
            r = re.search(r"RSSI: .*?\((-?\d+)\)", m.group(3)) or re.search(r"RSSI: (-?\d+)$", m.group(3).strip())
            if r:
                seen[mac] = int(r.group(1))
            else:
                seen.setdefault(mac, None)
        return seen

    def _connect(self, mac):
        run_cmd(["bluetoothctl", "trust", mac], 10)
        run_cmd(["bluetoothctl", "connect", mac], 25)
        return self._is_connected(mac)

    # ------------------------------------------------------------- the loop
    def _loop(self):
        while True:
            try:
                self.wake.clear()
                self._step()
            except Exception as e:          # never let the pedal thread die
                log(f"pedal: {e}")
                time.sleep(5)

    def _step(self):
        if not self._bt_power():
            self.state = "bluetooth starting"
            self.wake.wait(2)
            return
        mac = self.target
        if mac and self._is_connected(mac):
            if not self.connected:
                log(f"connected to {self.target_name()} ({mac})")
            self.connected, self.state, self.misses = True, "connected", 0
            self.wake.wait(3)
            return
        if self.connected:
            log(f"{self.target_name()} disconnected")
        self.connected = False
        if not mac and self.devices:
            self.state = "no device selected"
            self.wake.wait(5)
            return
        self.state = "connecting" if mac else "searching"
        seen = self._scan(8)
        if self.target != mac:               # the user picked another device meanwhile
            return
        if mac:
            if self._connect(mac):
                self.misses = 0
                return
        else:
            found = [dict(i, rssi=seen[i["mac"]]) for i in (self._info(m) for m in seen)
                     if i["midi"] and i["name"] == PEDAL_NAME]
            for cand in sorted(found, key=lambda i: -(i["rssi"] if i["rssi"] is not None else -999)):
                log(f"found {PEDAL_NAME} {cand['mac']} – connecting")
                if self._connect(cand["mac"]):
                    with self.lock:
                        if self.target is None and not self.devices:
                            self.devices = [{"mac": cand["mac"], "name": cand["name"],
                                             "added": time.strftime("%Y-%m-%d %H:%M")}]
                            self.default = self.target = cand["mac"]
                            self._save()
                    log(f"{PEDAL_NAME} {cand['mac']} saved as startup device")
                    self.misses = 0
                    return
        # device off or out of range: back off so scanning doesn't keep the
        # shared Wi-Fi/Bluetooth radio busy (keeps the hotspot smooth)
        self.misses += 1
        self.wake.wait(min(2 + 3 * self.misses, 20))

    # --------------------------------------------------------- user actions
    def scan(self):
        """Nearby devices for the page: MIDI-capable first, strongest first."""
        if not self._bt_power():
            raise ValueError("Bluetooth is starting, try again in a few seconds")
        seen = self._scan(8)
        with self.lock:
            saved = {d["mac"] for d in self.devices}
        res = []
        for mac in list(seen) + sorted(saved - set(seen)):   # seen now, plus saved ones (e.g. connected)
            i = self._info(mac)
            i.update(rssi=seen.get(mac, i["rssi"]), saved=mac in saved,
                     startup=mac == self.default, active=mac == self.target)
            res.append(i)
        res.sort(key=lambda i: (not i["midi"], -(i["rssi"] if i["rssi"] is not None else -999)))
        return res

    def connect_now(self, mac, make_default=False, name=None):
        mac = str(mac).upper()
        if not MAC_RE.match(mac):
            raise ValueError("invalid device address")
        if not name:
            name = self._info(mac)["name"]
        with self.lock:
            if mac not in [d["mac"] for d in self.devices]:
                self.devices.append({"mac": mac, "name": name, "added": time.strftime("%Y-%m-%d %H:%M")})
            if make_default or self.default is None:
                self.default = mac
            old, self.target = self.target, mac
            self._save()
        if old and old != mac:
            run_cmd(["bluetoothctl", "disconnect", old], 15)
        self.connected, self.state, self.misses = False, "connecting", 0
        log(f"switching to {name} ({mac})" + (" – startup device" if mac == self.default else ""))
        self.wake.set()

    def set_default(self, mac):
        mac = str(mac).upper()
        with self.lock:
            if mac not in [d["mac"] for d in self.devices]:
                raise ValueError("that device is not saved")
            self.default = mac
            if self.target is None:
                self.target = mac
            self._save()
        log(f"startup device: {self.name_of(mac)} ({mac})")
        self.wake.set()

    def forget(self, mac=None):
        with self.lock:
            mac = str(mac or self.target or "").upper()
            if not mac:
                return
            self.devices = [d for d in self.devices if d["mac"] != mac]
            if self.default == mac:
                self.default = self.devices[0]["mac"] if self.devices else None
            was_target = self.target == mac
            if was_target:
                self.target = self.default
            self._save()
        run_cmd(["bluetoothctl", "disconnect", mac], 15)
        run_cmd(["bluetoothctl", "remove", mac], 15)
        if was_target:
            self.connected, self.misses = False, 0
            self.state = "connecting" if self.target else "searching"
        log(f"forgot {mac}" + ("" if self.devices else f" – searching for a {PEDAL_NAME}"))
        self.wake.set()

    def status(self):
        with self.lock:
            devs = [dict(d, startup=d["mac"] == self.default, active=d["mac"] == self.target)
                    for d in self.devices]
        return {"name": self.target_name(), "mac": self.target, "state": self.state,
                "connected": self.connected, "default": self.default, "devices": devs}


PEDAL = None


# ---------------------------------------------------------------------- MIDI
class Midi:
    """Talks to the pedal's ALSA sequencer port (created by BlueZ BLE-MIDI)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.cc = {cc: on for cc, on in EFFECTS.values()}   # assume all on until the pedal tells us
        self.program = None
        self.port = None          # "client:port"
        self.connected = False
        self._dump = None
        threading.Thread(target=self._watch, daemon=True).start()

    # --- discovery / connection status
    @staticmethod
    def _find_port():
        try:
            out = subprocess.run(["aconnect", "-l"], capture_output=True, text=True, timeout=5).stdout
        except Exception:
            return None
        ids = re.findall(r"^client (\d+): '" + re.escape(PEDAL.target_name()) + "'", out, re.M)
        return f"{ids[0]}:0" if len(ids) == 1 else None

    def _watch(self):
        mac = None
        while True:
            if PEDAL.target != mac:           # another device: its state is unknown again
                mac = PEDAL.target
                with self.lock:
                    self.cc = {cc: on for cc, on in EFFECTS.values()}
                    self.program = None
            port = self._find_port() if PEDAL.connected else None
            if port != self.port:
                self.port = port
                log(f"pedal MIDI port {'found at ' + port if port else 'lost'}")
                self._restart_dump()
            self.connected = port is not None
            time.sleep(3)

    # --- incoming MIDI keeps our copy of the pedal state current
    def _restart_dump(self):
        if self._dump:
            self._dump.kill()
            self._dump = None
        if self.port:
            self._dump = subprocess.Popen(["stdbuf", "-oL", "aseqdump", "-p", self.port],
                                          stdout=subprocess.PIPE, text=True)
            threading.Thread(target=self._read_dump, args=(self._dump,), daemon=True).start()

    def _read_dump(self, proc):
        cc_re = re.compile(r"Control change\s+\d+, controller (\d+), value (\d+)")
        pc_re = re.compile(r"Program change\s+\d+, program (\d+)")
        for line in proc.stdout:
            m = cc_re.search(line)
            if m:
                with self.lock:
                    self.cc[int(m.group(1))] = int(m.group(2))
                continue
            m = pc_re.search(line)
            if m:
                with self.lock:
                    self.program = int(m.group(1))

    # --- sending
    def _send(self, *data):
        if not self.port:
            log("pedal not connected – command dropped")
            return False
        hexs = " ".join(f"{b:02X}" for b in data)
        r = subprocess.run(["aseqsend", "-p", self.port, hexs], capture_output=True, text=True)
        if r.returncode != 0:
            log(f"send failed: {r.stderr.strip()}")
            return False
        return True

    def set_cc(self, cc, value):
        if self._send(0xB0 | MIDI_CH, cc, value):
            with self.lock:
                self.cc[cc] = value

    def effect_on(self, eff):
        cc, dflt = EFFECTS[eff]
        return bool(self.cc.get(cc, dflt) & 64)

    def set_effect(self, eff, on):
        cc, dflt = EFFECTS[eff]
        cur = self.cc.get(cc, dflt)
        self.set_cc(cc, (cur | 64) if on else (cur & ~64))

    def program_change(self, p):
        p = max(0, min(127, p))
        if self._send(0xC0 | MIDI_CH, p):
            with self.lock:
                self.program = p


MIDI = None


def run_action(act, phase="fire"):
    """phase: 'fire' for normal actions, 'down'/'up' for momentary."""
    t = act["type"]
    if t == "none":
        return
    eff = act.get("effect")
    if t == "toggle":
        on = not MIDI.effect_on(eff)
        MIDI.set_effect(eff, on)
        log(f"{EFFECT_NAMES[eff]} {'ON' if on else 'OFF'}")
    elif t == "momentary":
        if phase == "up":
            MIDI.set_effect(eff, False); log(f"{EFFECT_NAMES[eff]} OFF (released)")
        else:
            MIDI.set_effect(eff, True); log(f"{EFFECT_NAMES[eff]} ON (held)")
    elif t in ("on", "off"):
        MIDI.set_effect(eff, t == "on"); log(f"{EFFECT_NAMES[eff]} {t.upper()}")
    elif t in ("all_off", "all_on"):
        for e in EFFECTS:
            MIDI.set_effect(e, t == "all_on")
        log("all effects " + ("ON" if t == "all_on" else "OFF"))
    elif t in ("preset_next", "preset_prev"):
        cur = MIDI.program if MIDI.program is not None else 0
        MIDI.program_change(cur + (1 if t == "preset_next" else -1))
        log(f"preset {MIDI.program}")
    elif t == "preset":
        MIDI.program_change(act["preset"]); log(f"preset {act['preset']}")


# ---------------------------------------------------------------------- GPIO
class Gpio:
    """Polls the button pins (active low, internal pull-ups)."""

    def __init__(self):
        self.backend = "none"
        self.h = None
        self.claimed = set()
        try:
            import lgpio
            self.lg = lgpio
            self.h = lgpio.gpiochip_open(0)
            self.backend = "lgpio"
        except Exception as e:
            log(f"GPIO unavailable ({e}); web test pad still works")

    def setup(self, pins):
        if not self.h:
            return
        for p in list(self.claimed - set(pins)):
            try: self.lg.gpio_free(self.h, p)
            except Exception: pass
            self.claimed.discard(p)
        for p in pins:
            if p not in self.claimed:
                self.lg.gpio_claim_input(self.h, p, self.lg.SET_PULL_UP)
                self.claimed.add(p)

    def pressed(self, pin):
        if not self.h:
            return False
        return self.lg.gpio_read(self.h, pin) == 0


# -------------------------------------------------------------------- engine
class Engine:
    POLL = 0.005
    DEBOUNCE = 0.020

    def __init__(self, gpio):
        self.gpio = gpio
        self.lock = threading.RLock()
        self.sim = {}            # button index -> simulated pressed state (web test pad)
        self.apply(load_config())
        threading.Thread(target=self._loop, daemon=True).start()

    def apply(self, cfg):
        with self.lock:
            self.cfg = cfg
            self.prof = cfg["profiles"][cfg["active_profile"]]
            n = len(self.prof["buttons"])
            self.raw = [False] * n
            self.stable = [False] * n
            self.changed_at = [0.0] * n
            self.st = {}         # pressed buttons: i -> dict(t, resolved, consumed, momentary)
            self.sim = {}
            self.gpio.setup([b["gpio"] for b in self.prof["buttons"]])
            # A pin that already reads "pressed" (stuck or wired wrong) must not fire:
            # start from the current state, so it only acts after a release + press.
            time.sleep(0.05)
            for i, b in enumerate(self.prof["buttons"]):
                self.raw[i] = self.stable[i] = self.gpio.pressed(b["gpio"])
                self.changed_at[i] = time.monotonic()
            stuck = [i + 1 for i, s in enumerate(self.stable) if s]
            if stuck:
                log(f"warning: footswitch {stuck} reads pressed at start (check wiring) – ignored until released")
        log(f"profile {cfg['active_profile']} buttons active")

    def in_combo(self, i):
        return any(i in c["buttons"] for c in self.prof["combos"])

    def _loop(self):
        while True:
            now = time.monotonic()
            with self.lock:
                for i, b in enumerate(self.prof["buttons"]):
                    r = self.gpio.pressed(b["gpio"]) or self.sim.get(i, False)
                    if r != self.raw[i]:
                        self.raw[i] = r
                        self.changed_at[i] = now
                    if r != self.stable[i] and now - self.changed_at[i] >= self.DEBOUNCE:
                        self.stable[i] = r
                        (self._down if r else self._up)(i, now)
                self._tick(now)
            time.sleep(self.POLL)

    def _down(self, i, now):
        self.st[i] = {"t": now, "resolved": False, "consumed": False, "momentary": False}
        live = [j for j, s in self.st.items() if not s["resolved"] and not s["consumed"]
                and now - s["t"] <= self.cfg["combo_ms"] / 1000]
        for c in self.prof["combos"]:
            if sorted(live) == c["buttons"]:
                for j in live:
                    self.st[j]["consumed"] = True
                log("combo " + "+".join(str(j + 1) for j in live))
                run_action(c["action"])
                return
        b = self.prof["buttons"][i]
        if b["press"]["type"] == "momentary" and not self.in_combo(i):
            self.st[i].update(resolved=True, momentary=True)
            run_action(b["press"], "down")

    def _tick(self, now):
        for i, s in self.st.items():
            if s["resolved"] or s["consumed"]:
                continue
            b = self.prof["buttons"][i]
            age = now - s["t"]
            if b["hold"]["type"] != "none":
                if age >= self.cfg["hold_ms"] / 1000:
                    s["resolved"] = True
                    log(f"button {i+1} hold")
                    run_action(b["hold"])
            elif not self.in_combo(i) or age >= self.cfg["combo_ms"] / 1000:
                s["resolved"] = True
                if b["press"]["type"] == "momentary":
                    s["momentary"] = True
                    run_action(b["press"], "down")
                else:
                    run_action(b["press"])

    def _up(self, i, now):
        s = self.st.pop(i, None)
        if not s or s["consumed"]:
            return
        b = self.prof["buttons"][i]
        if s["momentary"]:
            run_action(b["press"], "up")
        elif not s["resolved"]:          # short press on a button that has a hold action / combo
            if b["press"]["type"] == "momentary":
                run_action(b["press"], "down"); run_action(b["press"], "up")
            else:
                run_action(b["press"])

    def simulate(self, i, down):
        with self.lock:
            if 0 <= i < len(self.prof["buttons"]):
                self.sim[i] = down

    def pressed_list(self):
        with self.lock:
            return [bool(x) for x in self.stable]

    def pins_low(self):
        """Raw GPIO readings, for the page's wiring check (ignores the test pad)."""
        with self.lock:
            return [self.gpio.pressed(b["gpio"]) for b in self.prof["buttons"]]


ENGINE = None


# ---------------------------------------------------------------------- Wi-Fi
def hotspot_ssid():
    try:
        m = re.search(r'^SSID="?([^"\n]*)"?', open(HOTSPOT_PATH).read(), re.M)
        if m and m.group(1):
            return m.group(1)
    except OSError:
        pass
    return "STOMPBOX-CTRL"


class Wifi:
    """Lets the page put the Pi's own Wi-Fi (wlan0) on a network.

    The Pi Zero W has one radio: the STOMPBOX-CTRL hotspot (uap0) must share the
    channel of the network wlan0 joins, so the hotspot is stopped while switching
    and restarted afterwards on the new channel.
    """
    AP_CONF = "/run/stompbox/hostapd.conf"

    def __init__(self):
        self.lock = threading.Lock()
        self.op = {"state": "idle", "message": ""}

    @staticmethod
    def _run(args, timeout=30):
        try:
            r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
            return r.returncode, (r.stdout or "") + (r.stderr or "")
        except subprocess.TimeoutExpired:
            return 124, "timed out"
        except FileNotFoundError as e:
            return 127, str(e)

    @staticmethod
    def _split(line):
        # nmcli -t escapes ':' as '\:' inside fields
        return [f.replace("\\:", ":").replace("\\\\", "\\") for f in re.split(r"(?<!\\):", line)]

    def current(self):
        rc, out = self._run(["nmcli", "-t", "-f", "GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS",
                             "dev", "show", "wlan0"], 10)
        info = {"connected": False, "ssid": None, "ip": None, "channel": None, "connection": None}
        for line in out.splitlines():
            k, _, v = line.partition(":")
            if k == "GENERAL.STATE":
                info["connected"] = v.startswith("100")
            elif k == "GENERAL.CONNECTION" and v:
                info["connection"] = v
            elif k.startswith("IP4.ADDRESS") and v and not info["ip"]:
                info["ip"] = v.split("/")[0]
        rc, out = self._run(["iw", "dev", "wlan0", "info"], 5)
        m = re.search(r"^\s*ssid (.+)$", out, re.M)
        if m and info["connected"]:
            info["ssid"] = m.group(1)
        m = re.search(r"channel (\d+)", out)
        if m and info["connected"]:
            info["channel"] = int(m.group(1))
        return info

    def hotspot(self):
        ch = None
        try:
            m = re.search(r"^channel=(\d+)", open(self.AP_CONF).read(), re.M)
            ch = int(m.group(1)) if m else None
        except OSError:
            pass
        up = subprocess.run(["pgrep", "-x", "hostapd"], capture_output=True).returncode == 0
        return {"ssid": hotspot_ssid(), "ip": "192.168.4.1", "channel": ch, "running": up}

    def saved(self, fresh=False):
        # cached: each nmcli call costs noticeable CPU on a Pi Zero
        if not fresh and getattr(self, "_saved", None) and time.time() - self._saved[0] < 20:
            return self._saved[1]
        rc, out = self._run(["nmcli", "-t", "-f", "NAME,TYPE", "con", "show"], 10)
        res = []
        for line in out.splitlines():
            f = self._split(line)
            if len(f) >= 2 and f[1] == "802-11-wireless":
                rc2, ssid = self._run(["nmcli", "-g", "802-11-wireless.ssid", "con", "show", f[0]], 10)
                res.append({"name": f[0], "ssid": ssid.strip() or f[0]})
        self._saved = (time.time(), res)
        return res

    def scan(self):
        rc, out = self._run(["nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY,CHAN", "dev", "wifi",
                             "list", "ifname", "wlan0", "--rescan", "yes"], 40)
        best = {}
        for line in out.splitlines():
            f = self._split(line)
            if len(f) < 5 or not f[1]:
                continue
            n = {"ssid": f[1], "signal": int(f[2] or 0), "secure": f[3] not in ("", "--"),
                 "channel": int(f[4] or 0), "in_use": f[0] == "*"}
            if n["ssid"] not in best or n["signal"] > best[n["ssid"]]["signal"]:
                best[n["ssid"]] = n
        return sorted(best.values(), key=lambda n: (-n["in_use"], -n["signal"]))

    def state(self):
        with self.lock:
            op = dict(self.op)
        return {"current": self.current(), "hotspot": self.hotspot(), "saved": self.saved(), "op": op}

    def _set(self, state, message, **extra):
        self._saved = None
        with self.lock:
            self.op = {"state": state, "message": message, "at": time.strftime("%H:%M:%S"), **extra}
        log("wifi: " + message)

    def start(self, kind, **kw):
        with self.lock:
            if self.op.get("state") == "working":
                raise ValueError("a Wi-Fi change is already in progress")
            self.op = {"state": "working", "message": "starting…"}
        threading.Thread(target=self._do, args=(kind,), kwargs=kw, daemon=True).start()

    def _do(self, kind, ssid=None, password=None, hidden=False, name=None):
        time.sleep(2)                                   # let the HTTP reply reach the phone first
        prev = self.current().get("connection")
        before = {s["name"] for s in self.saved(fresh=True)}
        target = ssid or name
        if kind == "forget":
            rc, out = self._run(["nmcli", "con", "delete", name], 30)
            if rc == 0:
                self._set("ok", f"forgot {name}")
            else:
                self._set("error", f"could not forget {name}: {out.strip()[:200]}")
            if name == prev:
                self._restart_hotspot()       # back to channel 6 on its own
            return
        try:
            self._set("working", f"connecting to {target}… the hotspot restarts, rejoin {hotspot_ssid()} in ~30 s")
            self._run(["systemctl", "stop", "stompbox-ap.service"], 30)
            time.sleep(1)
            if kind == "saved":
                rc, out = self._run(["nmcli", "-w", "40", "con", "up", name, "ifname", "wlan0"], 50)
            else:
                args = ["nmcli", "-w", "40", "dev", "wifi", "connect", ssid, "ifname", "wlan0"]
                if password:
                    args += ["password", password]
                if hidden:
                    args += ["hidden", "yes"]
                rc, out = self._run(args, 50)
            cur = self.current()
            if rc == 0 and cur["connected"]:
                ip = cur["ip"] or "?"
                self._set("ok", f"connected to {cur['ssid'] or target} – settings page at http://{ip}",
                          ssid=cur["ssid"], ip=ip)
            else:
                reason = out.strip().splitlines()[-1] if out.strip() else "unknown error"
                if "Secrets were required" in out or "802-11-wireless-security" in out:
                    reason = "wrong password"
                for s in self.saved():                  # drop the half-made profile
                    if s["name"] not in before:
                        self._run(["nmcli", "con", "delete", s["name"]], 30)
                if prev:
                    self._run(["nmcli", "-w", "40", "con", "up", prev, "ifname", "wlan0"], 50)
                self._set("error", f"could not connect to {target}: {reason[:160]}"
                          + (f" – back on {prev}" if prev else ""))
        finally:
            self._restart_hotspot()

    def _restart_hotspot(self):
        self._run(["systemctl", "restart", "stompbox-ap.service"], 90)


WIFI = Wifi()


# ---------------------------------------------------------------------- HTTP
class Handler(BaseHTTPRequestHandler):
    server_version = "stompboxd"

    def log_message(self, *args):
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    PORTAL = "http://192.168.4.1/"

    def _foreign_host(self):
        """True when the request was meant for another site (phone connectivity checks
        on the hotspot): those get redirected so the phone opens the settings page."""
        host = (self.headers.get("Host") or "").rsplit(":", 1)[0].strip("[]").lower()
        if not host or re.fullmatch(r"[\d.]+|[0-9a-f:]+", host):
            return False
        local = {"stompbox", "stompbox.local", "localhost", socket.gethostname().lower(),
                 socket.gethostname().lower() + ".local"}
        return host not in local

    def _redirect(self):
        body = (f'<!doctype html><meta http-equiv="refresh" content="0;url={self.PORTAL}">'
                f'<a href="{self.PORTAL}">STOMPBOX settings</a>').encode()
        self.send_response(302)
        self.send_header("Location", self.PORTAL)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self):
        if not self._foreign_host() and self.path.split("?")[0] in ("/", "/index.html"):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
        else:
            self._redirect()

    def do_GET(self):
        path = self.path.split("?")[0]
        if self._foreign_host():
            self._redirect()
        elif path in ("/", "/index.html"):
            with open(os.path.join(WEB_DIR, "index.html"), "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/api/config":
            self._json(ENGINE.cfg)
        elif path == "/api/default":
            self._json(default_config())
        elif path == "/api/wifi":
            self._json(WIFI.state())
        elif path == "/api/status":
            with MIDI.lock:
                effects = {e: bool(MIDI.cc.get(cc, d) & 64) for e, (cc, d) in EFFECTS.items()}
                program = MIDI.program
            with LOG_LOCK:
                lg = list(LOG)[:15]
            self._json({"version": VERSION,
                        "pedal_connected": MIDI.connected, "midi_port": MIDI.port,
                        "pedal": PEDAL.status(),
                        "program": program, "effects": effects,
                        "pressed": ENGINE.pressed_list(), "pins_low": ENGINE.pins_low(),
                        "gpio": ENGINE.gpio.backend,
                        "profile": ENGINE.cfg["active_profile"], "log": lg})
        else:
            self._redirect()      # unknown path (e.g. /generate_204, /hotspot-detect.html)

    def do_POST(self):
        try:
            if self.path == "/api/config":
                cfg = validate_config(self._body())
                save_config(cfg)
                ENGINE.apply(cfg)
                log("settings saved")
                self._json({"ok": True, "config": cfg})
            elif self.path == "/api/sim":
                d = self._body()
                ENGINE.simulate(int(d["button"]), bool(d["down"]))
                self._json({"ok": True})
            elif self.path == "/api/wifi/scan":
                self._json({"ok": True, "networks": WIFI.scan()})
            elif self.path == "/api/wifi/connect":
                d = self._body()
                ssid = str(d.get("ssid", "")).strip()
                pw = str(d.get("password", ""))
                if not 1 <= len(ssid) <= 32:
                    raise ValueError("network name must be 1-32 characters")
                if pw and not 8 <= len(pw) <= 63:
                    raise ValueError("Wi-Fi passwords are 8-63 characters")
                WIFI.start("new", ssid=ssid, password=pw or None, hidden=bool(d.get("hidden")))
                self._json({"ok": True})
            elif self.path == "/api/wifi/up":
                WIFI.start("saved", name=str(self._body()["name"]))
                self._json({"ok": True})
            elif self.path == "/api/wifi/forget":
                WIFI.start("forget", name=str(self._body()["name"]))
                self._json({"ok": True})
            elif self.path == "/api/action":
                act = validate_action(self._body())
                threading.Thread(target=run_action, args=(act,), daemon=True).start()
                self._json({"ok": True})
            elif self.path == "/api/cc":                     # raw Control Change (stomp CLI)
                d = self._body()
                cc, val = int(d["cc"]), int(d["value"])
                if not (0 <= cc <= 127 and 0 <= val <= 127):
                    raise ValueError("cc and value must be 0-127")
                MIDI.set_cc(cc, val)
                self._json({"ok": True, "connected": MIDI.connected})
            elif self.path == "/api/pedal/forget":          # forget the device in use
                threading.Thread(target=PEDAL.forget, daemon=True).start()
                self._json({"ok": True})
            elif self.path == "/api/bt/scan":
                self._json({"ok": True, "devices": PEDAL.scan()})
            elif self.path == "/api/bt/connect":
                d = self._body()
                PEDAL.connect_now(d["mac"], bool(d.get("startup")), d.get("name"))
                self._json({"ok": True})
            elif self.path == "/api/bt/startup":
                PEDAL.set_default(self._body()["mac"])
                self._json({"ok": True})
            elif self.path == "/api/bt/forget":
                d = self._body()
                threading.Thread(target=PEDAL.forget, args=(d["mac"],), daemon=True).start()
                self._json({"ok": True})
            else:
                self._json({"error": "not found"}, 404)
        except (ValueError, KeyError, TypeError) as e:
            self._json({"ok": False, "error": str(e)}, 400)


def main():
    global MIDI, ENGINE, PEDAL
    log(f"stompboxd {VERSION} starting")
    for tool in ("aseqsend", "aseqdump", "aconnect", "bluetoothctl", "nmcli", "iw"):
        if not shutil.which(tool):
            log(f"warning: {tool} not found")
    PEDAL = Pedal()
    MIDI = Midi()
    ENGINE = Engine(Gpio())
    log(f"settings page on port {HTTP_PORT}")
    ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
