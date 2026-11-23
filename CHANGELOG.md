# Changelog

All notable changes to this project. Newest first.
Format: [Keep a Changelog](https://keepachangelog.com/), versions follow `VERSION` in `software/stompboxd.py`.

## [Unreleased]

### Added
- 3D-printable case for the ESP32-C3 SuperMini with 4 footswitches (`hardware/case/esp32-4-buttons`): 144 × 32 × 43 mm, same 40 mm button spacing as the Pi slim case, 5 mm lower, board cradle with USB-C opening, base + labelled lid + plain lid (STL + SCAD).

## [1.3.1] – 2026-11-23

### Changed
- ESP32-C3: Wi-Fi transmit power limited to 8.5 dBm (`WIFI_TX_POWER` in `config.h`) – many SuperMini boards reset or lose Wi-Fi at full power.
- ESP32-C3 README: flashing with Espressif's web flasher (works without GitHub Pages), troubleshooting for USB connect/disconnect loops, and a clearer wiring note: the published SuperMini pinouts disagree on left/right, so the GPIO labels printed on the board are what count (diagram caption updated).

## [1.3.0] – 2026-11-04

Brings the Raspberry Pi and ESP32-C3 versions closer together.

### Added
- **Hotspot name and password on the settings page** (Wi-Fi → Hotspot), on both platforms; option for an open network. API `GET/POST /api/hotspot`.
- ESP32-C3: **joins any saved Wi-Fi network in range** whenever the connection has been lost for 30 s (before: only the strongest one at start-up), like the Pi.
- ESP32-C3: **16 unit tests** run on a computer (`pio test -e native`): the same footswitch scenarios as the Pi tests, settings validation, and the BLE-MIDI parser checked against packets recorded from the pedal.
- Pi: tests for the hotspot settings file (6 tests).

### Changed
- ESP32-C3: BLE-MIDI parsing moved to `src/blemidi.h` so it can be tested.
- Pi: `hotspot.conf` is read with proper shell-quoting rules (names with spaces or quotes work).

## [1.2.0] – 2026-11-04

### Added
- **ESP32-C3 version** in `esp32-c3/` (own README): firmware with the same features as the Pi version – pedal over Bluetooth MIDI with device manager, 4/5 footswitches with press/hold/combos, own hotspot with captive portal, joining Wi-Fi networks, same settings page and API – plus a browser flasher (ESP Web Tools), ready-to-flash binaries, firmware update over Wi-Fi (`/update`), factory reset (BOOT 10 s) and a SuperMini wiring diagram. Compiles; not yet tested on hardware.

### Changed
- Settings page is platform-neutral: footswitch pin list can come from the device (`pins_available` in `/api/config`), wording says "controller" instead of "Pi", footer shows the platform.

## [1.1.0] – 2026-11-04

### Added
- **Bluetooth device manager** on the settings page (*Pedal · Bluetooth*): search for nearby Bluetooth MIDI devices, **Connect now**, **Connect + always at startup**, and a list of saved devices with **Use at startup** and **Forget**.
- `pedal.json` now stores several saved devices and a startup device; the 1.0 format is read and converted automatically.
- API: `/api/bt/scan`, `/api/bt/connect`, `/api/bt/startup`, `/api/bt/forget`; `/api/status` reports the device list.
- `stomp status` lists saved devices and marks the startup device and the one in use.
- Tests for the device list (`tests/test_devices.py`).

### Changed
- The MIDI port is found by the name of the device in use (not a fixed name).
- Effect states are reset to "unknown / all on" when switching to another device.

### Removed
- The single "Pair a different pedal" button (replaced by the device manager).

## [1.0.0] – 2026-11-04

First release, built and tested on a Raspberry Pi Zero W Rev 1.1 (Raspberry Pi OS Trixie) with a JME STOMPBOX.

### Added
- `stompboxd` controller service:
  - finds, pairs (trust) and keeps the JME STOMPBOX connected over Bluetooth MIDI; locks to the first pedal found by address;
  - 4- and 5-footswitch profiles on GPIO with press, hold and multi-button combo actions;
  - actions: toggle, on-while-held, on, off, all on/off, next/previous preset, go to preset;
  - tracks the pedal's effect values so toggling keeps the effect type of each slot;
  - ignores footswitches that read "pressed" at start-up (stuck pins).
- Settings page (port 80): pedal status and re-pairing, Wi-Fi scan/join/forget, profiles, buttons, combos, timing, test pad with live wiring check, activity log.
- Own Wi-Fi hotspot `STOMPBOX-CTRL` (hostapd on `uap0`) running alongside the normal Wi-Fi on the same radio, with captive portal (the page opens automatically) and a safety net that removes the hotspot if it disconnects the Pi from its network.
- `stomp` command-line tool, `stompbox-update`, idempotent `install.sh` (one-line install from GitHub) and `uninstall.sh`.
- 3D-printable cases (all ≤ 170 mm): 5-button, 4-button and 4-button slim, each with base, labelled lid and plain lid (STL + OpenSCAD).
- Wiring diagram, JME STOMPBOX MIDI map, tests for the footswitch logic.
