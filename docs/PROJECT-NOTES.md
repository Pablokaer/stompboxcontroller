# Project notes

Background, findings and decisions collected while building and testing the
controller. The READMEs say *how* to use things; this file says *why* they are
the way they are, what has been tested on real hardware and what is still open.

## History

| Version / item | What it added |
|---|---|
| 1.0.0 | Raspberry Pi Zero W controller: BLE-MIDI connection at boot, 5 footswitches, settings page on its own hotspot, captive portal, home Wi-Fi, installer. Cases `5-buttons`, `4-buttons`, `4-buttons-slim`. |
| 1.1.0 | Bluetooth device manager: saved devices, startup device, "connect now", forget. |
| 1.2.0 | ESP32-C3 SuperMini port (`esp32-c3/`) with the same page and API. |
| 1.3.0 | Hotspot name/password on the page (both platforms), ESP32 rejoins saved networks, ESP32 unit tests. |
| 1.3.1 | ESP32 lowers Wi-Fi TX power (SuperMini brown-outs), flashing and troubleshooting docs. |
| Cases | `esp32-4-buttons` (144 × 32 × 43 mm, 40 mm pitch) and `esp32-4-buttons-compact` (127 × 27 × 36 mm, 35 mm pitch, board with headers up + Dupont jumpers). An anti-vandal-button compact version was tried and dropped: conventional footswitches are preferred. |

## The pedal (JME STOMPBOX)

- **BLE-MIDI**, standard service `03b80e5a-…` / characteristic `7772e5db-…`. The
  map in [MIDI-MAP.md](MIDI-MAP.md) was found by sniffing the official app's
  traffic and then sending each CC to the pedal while watching its screen.
- Effect CCs: 3 MOD, 4 DELAY, 5 REVERB, 6 GATE, 7 EQ. Value bit 64 = on, the low
  bits = effect type, so a toggle must keep the type bits. Program Change =
  preset, CC 59 = screen page.
- **It accepts only one Bluetooth connection at a time.** While the Pi (or a
  phone app) is connected, the ESP32 can't connect, and the other way round.
  Switch off or unplug the controller you're not using. This is the first thing
  to check when the ESP32 LED keeps blinking.
- It doesn't need bonding: trust + connect is enough. It doesn't acknowledge
  commands, so the controller can't confirm a change took effect.
- It also has a second, audio-only Bluetooth address that isn't MIDI; the device
  search hides it.

## Raspberry Pi findings

- **Hotspot**: a NetworkManager hotspot on the same radio as the home Wi-Fi was
  visible but phones failed WPA authentication ("802.1X supplicant took too
  long"). Fixed by running `hostapd` on a virtual interface `uap0` (same channel
  as the home network, one radio), marked unmanaged for NetworkManager.
- **Captive portal**: dnsmasq answers every DNS name with the Pi; nftables
  redirects HTTP to the page, rejects HTTPS so phones fall back fast, and blocks
  forwarding.
- `aseqsend` needs alsa-utils 1.2.11+, i.e. Raspberry Pi OS based on Debian 13
  "Trixie".
- **GPIO22 (pin 15) was stuck low** on the prototype Pi; that led to the
  stuck-pin protection (a pin that reads "pressed" at start-up is ignored until
  released).
- **SSH refusals** (`kex_exchange_identification: Connection closed`) after many
  interrupted logins come from OpenSSH's per-source penalties. Wait ~10 minutes
  or restart the Pi; for scripted access use one multiplexed connection
  (`ControlMaster`).

## ESP32-C3 SuperMini findings

- **Why the full image goes at `0x0`**: the ESP32-C3 ROM loads the second-stage
  bootloader from flash offset `0x0` (classic ESP32 and ESP32-S2 use `0x1000`).
  `stompbox-esp32c3-full.bin` is a merged image laid out as
  `0x0` bootloader · `0x8000` partition table · `0xE000` OTA data · `0x10000`
  app, so it must be written starting at `0x0`. The `-app.bin` is only the
  application and goes at `0x10000` (or through the page's update).
- **A brand-new board connects and disconnects every second** on USB: with no
  firmware it keeps resetting. Hold BOOT while plugging in, then flash.
- **Flashing needs Chrome or Edge on a computer.** Claude's built-in browser
  has no Web Serial port picker (`requestPort` fails with "No port selected by
  the user"), and Firefox/Safari don't support Web Serial.
- The tested board reports *ESP32-C3 rev v0.4, 4 MB embedded flash*.
- **Wi-Fi power**: many SuperMini boards brown out at full TX power;
  `WIFI_TX_POWER` is 8.5 dBm by default.
- **Pinout**: SuperMini clones differ in silk-screen placement (labels are often
  on the underside only), so wire by the printed GPIO numbers, not by position
  in a diagram. Avoid GPIO 2, 8, 9 (strapping), 18/19 (USB), 11–17 (flash).
  LED = GPIO 8 (active low), BOOT = GPIO 9.
- Status LED: blinking = searching for the pedal, steady = connected.

## Hardware test status

| Item | Status |
|---|---|
| Pi: pedal connection at boot, MOD/DELAY/REVERB/EQ/GATE toggles | Tested on the pedal |
| Pi: hotspot, captive portal, settings page, home Wi-Fi | Tested with a phone |
| Pi: footswitches | Tested with jumper wires (GPIO22 faulty on the prototype) |
| Pi: Bluetooth device manager (1.1.0) | Tested |
| ESP32-C3: flashing (merged image at `0x0`) | Done, board boots, LED blinks |
| ESP32-C3: pedal connection | **Not yet confirmed** – retest with the Pi switched off |
| ESP32-C3: settings page, hotspot, home Wi-Fi, footswitches | Not yet tested on hardware (unit tests pass) |
| Printed cases | Not printed yet – check switch depth/nut size against your parts |

## Open items

- Push the latest commits to GitHub (`main` there is at 1.3.0).
- Switch on GitHub Pages (Settings → Pages → `main`, `/ (root)`) so the
  browser flasher URL works.
- Update the Pi to the current version: `sudo stompbox-update`.
- ESP32-C3: with the Pi off, confirm the LED goes steady, join it to the home
  Wi-Fi from its page, then work through the testing checklist in
  [esp32-c3/README.md](../esp32-c3/README.md#testing-checklist).
