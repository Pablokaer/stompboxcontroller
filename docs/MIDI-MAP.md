# JME STOMPBOX – MIDI map

There is no public MIDI chart for this pedal. Everything below was found by
recording the MIDI messages the pedal sends over Bluetooth while its own
buttons were pressed (`aseqdump` + `btmon` on the Pi), then sending the same
messages back to confirm.

## Connection

| Item | Value |
|---|---|
| Bluetooth name | `JME STOMPBOX` |
| Interfaces | Two addresses per pedal: one **BLE-MIDI** (used here) and one classic Bluetooth audio (not used) |
| BLE-MIDI service UUID | `03b80e5a-ede8-4b33-a751-6ce34ec4c700` |
| On the Pi | BlueZ creates the ALSA sequencer port **`JME STOMPBOX Bluetooth`** (client number varies, e.g. `128:0`) |
| Pairing | The pedal does not bond; being *trusted* + *connected* is enough |
| MIDI channel | 1 |
| Acknowledgement | None – the pedal does not echo commands it receives |

## Effect on/off – Control Change (channel 1)

| Effect | CC | On | Off | Raw hex (on / off) |
|---|---|---|---|---|
| MOD    | 3 | 64 | 0 | `B0 03 40` / `B0 03 00` |
| DELAY  | 4 | 64 | 0 | `B0 04 40` / `B0 04 00` |
| REVERB | 5 | 65 | 1 | `B0 05 41` / `B0 05 01` |
| GATE   | 6 | 64 | 0 | `B0 06 40` / `B0 06 00` |
| EQ     | 7 | 64 | 0 | `B0 07 40` / `B0 07 00` |

**How the value works:** bit 64 is *on*; the low bits select the effect type
loaded in that slot. Reverb in the mapped preset is type 1, hence 65/1. Other
presets can use other types (MOD was seen from 64 to 76). To switch an effect
without changing its type, keep the low bits and add/remove 64 – e.g. MOD at
69 → off = 5, on = 69. **stompboxd does this automatically**: it tracks the
current values from the pedal's own messages.

## Other messages

| Message | Meaning |
|---|---|
| Program Change 0–127 | Preset select (the pedal reports it when you change preset) |
| After a preset change | The pedal sends the whole preset as CCs (0–59): handy to learn more parameters |
| CC 0, 1, 2 | Other modules (not identified yet) |
| CC 59 | Module shown on the pedal's screen: 1 home, 3 MOD, 4 DLY, 5 RVB, 6 GATE, 7 EQ (sent by the pedal, not needed to switch effects) |
| CC 8–58 | Effect parameters (knob values) – not mapped yet |

## Trying it from the Pi

```bash
stomp gate off              # through stompboxd (recommended)
stomp cc 6 64               # raw CC 6 = 64
aseqsend -p "JME STOMPBOX" B0 06 00        # without stompboxd
aseqdump -p "JME STOMPBOX"                 # watch what the pedal sends
```

## Mapping more parameters

1. Run `aseqdump -p "JME STOMPBOX"` (it can run while stompboxd is running) and turn one knob or press one button on the pedal.
2. Note the CC number and value range, then test it with `stomp cc <n> <value>`.
3. Add it to this file (and to `EFFECTS` in `software/stompboxd.py` if it's an on/off module), and update the README.
