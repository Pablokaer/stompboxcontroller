"""Tests for the hotspot settings file (written by the page, read by stompbox-ap.sh).

    python3 -m unittest discover -s tests -v
"""
import os, subprocess, sys, tempfile, unittest

os.environ.setdefault("STOMPBOX_ETC", tempfile.mkdtemp())
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "software"))
import stompboxd as s  # noqa: E402


class HotspotTest(unittest.TestCase):
    def setUp(self):
        if os.path.exists(s.HOTSPOT_PATH):
            os.remove(s.HOTSPOT_PATH)

    def test_defaults_without_file(self):
        self.assertEqual(s.read_hotspot()["SSID"], "STOMPBOX-CTRL")

    def test_roundtrip_and_shell_reads_same_values(self):
        name, pw = "Pablo's Rig \"A\"", "pa$$ w0rd`x"
        s.write_hotspot(name, pw, "IE")
        self.assertEqual((s.read_hotspot()["SSID"], s.read_hotspot()["PASSWORD"]), (name, pw))
        out = subprocess.run(["bash", "-c", f'. "{s.HOTSPOT_PATH}"; printf "%s\\n%s" "$SSID" "$PASSWORD"'],
                             capture_output=True, text=True).stdout
        self.assertEqual(out, f"{name}\n{pw}")    # stompbox-ap.sh sources this file

    def test_empty_password_keeps_current(self):
        self.assertEqual(s.validate_hotspot({"ssid": "X"}, "oldpassword"), ("X", "oldpassword"))

    def test_open_network(self):
        self.assertEqual(s.validate_hotspot({"ssid": "X", "open": True}, "oldpassword"), ("X", ""))

    def test_rejects_bad_values(self):
        for d in ({"ssid": ""}, {"ssid": "x" * 33}, {"ssid": "X", "password": "short"},
                  {"ssid": "X", "password": "ação-12345"}):
            with self.assertRaises(ValueError, msg=d):
                s.validate_hotspot(d, "oldpassword")

    def test_no_password_on_open_network_needs_one(self):
        with self.assertRaises(ValueError):
            s.validate_hotspot({"ssid": "X"}, "")


if __name__ == "__main__":
    unittest.main()
