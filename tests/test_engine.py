"""Tests for the footswitch logic (press / hold / momentary / combos) and settings validation.

Runs on any computer, no Pi or pedal needed:
    python3 -m unittest discover -s tests -v
"""
import os, sys, tempfile, threading, time, unittest

TMP = tempfile.mkdtemp()
os.environ["STOMPBOX_ETC"] = TMP
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "software"))
import stompboxd as s  # noqa: E402

SENT = []


class FakeMidi(s.Midi):
    """Records MIDI instead of sending it."""
    def __init__(self):
        self.lock = threading.Lock()
        self.cc = {cc: on for cc, on in s.EFFECTS.values()}
        self.program = 98
        self.port = "128:0"
        self.connected = True

    def _send(self, *data):
        SENT.append(" ".join(f"{b:02X}" for b in data))
        return True


class FakeGpio:
    backend = "test"
    def setup(self, pins): pass
    def pressed(self, pin): return False


class EngineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        s.MIDI = FakeMidi()
        cfg = s.default_config()
        p = cfg["profiles"]["4"]
        p["buttons"][1]["hold"] = {"type": "preset_next"}
        p["buttons"][2]["press"] = {"type": "momentary", "effect": "reverb"}
        p["combos"] = [{"buttons": [0, 3], "action": {"type": "all_off"}}]
        s.save_config(s.validate_config(cfg))
        cls.e = s.Engine(FakeGpio())

    def run_and_collect(self, fn):
        SENT.clear()
        fn()
        time.sleep(0.25)
        return list(SENT)

    def tap(self, i, secs=0.06):
        self.e.simulate(i, True); time.sleep(secs); self.e.simulate(i, False)

    def test_1_tap_toggles(self):
        self.assertEqual(self.run_and_collect(lambda: self.tap(0)), ["B0 03 00"])        # MOD off

    def test_2_tap_on_button_with_hold_fires_press(self):
        self.assertEqual(self.run_and_collect(lambda: self.tap(1, 0.1)), ["B0 04 00"])   # DELAY off

    def test_3_hold_fires_hold_action(self):
        self.assertEqual(self.run_and_collect(lambda: self.tap(1, 0.8)), ["C0 63"])      # preset 99

    def test_4_momentary_on_while_held(self):
        self.assertEqual(self.run_and_collect(lambda: self.tap(2, 0.4)), ["B0 05 41", "B0 05 01"])

    def test_5_combo(self):
        def combo():
            self.e.simulate(0, True); time.sleep(0.04); self.e.simulate(3, True)
            time.sleep(0.3); self.e.simulate(0, False); self.e.simulate(3, False)
        self.assertEqual(self.run_and_collect(combo),
                         ["B0 03 00", "B0 04 00", "B0 05 01", "B0 06 00", "B0 07 00"])

    def test_6_effect_type_bits_are_kept(self):
        s.MIDI.cc[3] = 69                     # MOD on, effect type 5
        self.assertEqual(self.run_and_collect(lambda: self.tap(0)), ["B0 03 05"])


class ValidationTest(unittest.TestCase):
    def test_bad_hold_time(self):
        with self.assertRaises(ValueError):
            s.validate_config({**s.default_config(), "hold_ms": 10})

    def test_duplicate_gpio(self):
        cfg = s.default_config()
        cfg["profiles"]["4"]["buttons"][1]["gpio"] = 17
        with self.assertRaises(ValueError):
            s.validate_config(cfg)

    def test_momentary_cannot_have_hold(self):
        cfg = s.default_config()
        cfg["profiles"]["4"]["buttons"][0].update(press={"type": "momentary", "effect": "mod"},
                                                   hold={"type": "all_off"})
        with self.assertRaises(ValueError):
            s.validate_config(cfg)

    def test_default_is_valid(self):
        self.assertEqual(s.validate_config(s.default_config())["active_profile"], "4")


if __name__ == "__main__":
    unittest.main()
