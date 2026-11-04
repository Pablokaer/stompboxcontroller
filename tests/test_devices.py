"""Tests for the Bluetooth device list (saved devices, startup device, switching, forgetting).

No Bluetooth needed: bluetoothctl calls are replaced by a fake.
    python3 -m unittest discover -s tests -v
"""
import json, os, sys, tempfile, unittest

os.environ.setdefault("STOMPBOX_ETC", tempfile.mkdtemp())
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "software"))
import stompboxd as s  # noqa: E402

A, B = "07:A3:9E:0B:96:8B", "11:22:33:44:55:66"
CALLS = []


def fake_run(args, timeout=30):
    CALLS.append(args)
    if args[:2] == ["bluetoothctl", "info"]:
        return 0, f"Device {args[2]} (public)\n\tName: Other Pedal\n"
    return 0, ""


class DeviceTest(unittest.TestCase):
    def setUp(self):
        s.run_cmd = fake_run
        s.log = lambda m: None
        CALLS.clear()
        if os.path.exists(s.PEDAL_PATH):
            os.remove(s.PEDAL_PATH)

    def saved(self):
        with open(s.PEDAL_PATH) as f:
            return json.load(f)

    def test_reads_old_format(self):
        devices, default = s.parse_pedal_file({"mac": A, "name": "JME STOMPBOX", "paired": "migrated"})
        self.assertEqual(default, A)
        self.assertEqual(devices[0]["name"], "JME STOMPBOX")

    def test_bad_default_falls_back_to_first_device(self):
        devices, default = s.parse_pedal_file({"default": "nope", "devices": [{"mac": A}, {"mac": "bad"}]})
        self.assertEqual([d["mac"] for d in devices], [A])
        self.assertEqual(default, A)

    def test_connect_now_keeps_startup_device(self):
        p = s.Pedal(start=False)
        p.connect_now(A, make_default=True, name="JME STOMPBOX")
        p.connect_now(B)                                   # use B now, A stays the startup device
        self.assertEqual((p.target, p.default), (B, A))
        self.assertEqual(self.saved()["default"], A)
        self.assertEqual(p.target_name(), "Other Pedal")   # name looked up via bluetoothctl info
        self.assertIn(["bluetoothctl", "disconnect", A], CALLS)

    def test_set_startup_and_reload(self):
        p = s.Pedal(start=False)
        p.connect_now(A, name="JME STOMPBOX")
        p.connect_now(B, name="Other Pedal")
        p.set_default(B)
        again = s.Pedal(start=False)                       # like a restart of the Pi
        self.assertEqual((again.default, again.target), (B, B))

    def test_forget_startup_device_moves_to_next(self):
        p = s.Pedal(start=False)
        p.connect_now(A, make_default=True, name="JME STOMPBOX")
        p.connect_now(B, name="Other Pedal")
        p.forget(A)
        self.assertEqual(p.default, B)
        self.assertEqual([d["mac"] for d in self.saved()["devices"]], [B])

    def test_forget_last_device_searches_again(self):
        p = s.Pedal(start=False)
        p.connect_now(A, make_default=True, name="JME STOMPBOX")
        p.forget()
        self.assertEqual((p.target, p.default, p.state), (None, None, "searching"))

    def test_scan_output_parsing(self):
        out = ("[CHG] Controller B8:27:EB:C4:B0:09 Discovering: yes\n"
               "[\x1b[0;92mNEW\x1b[0m] Device 07:A3:9E:0B:96:8B JME STOMPBOX\n"
               "[\x1b[0;93mCHG\x1b[0m] Device 07:A3:9E:0B:96:8B RSSI: 0xffffffc6 (-58)\n"
               "[NEW] Device F8:1B:E2:29:FC:79 Aura A40 Pro BLE\n"
               "[DEL] Device 11:11:11:11:11:11 gone\n")
        s.run_cmd = lambda args, timeout=30: (0, out)
        self.assertEqual(s.Pedal(start=False)._scan(1), {A: -58, "F8:1B:E2:29:FC:79": None})

    def test_rejects_bad_address(self):
        with self.assertRaises(ValueError):
            s.Pedal(start=False).connect_now("not-a-mac")


if __name__ == "__main__":
    unittest.main()
