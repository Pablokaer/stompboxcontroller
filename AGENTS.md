# Rules for changing this project

Applies to everyone – people and AI assistants (Claude, Copilot, …).

## Every change

1. **Update `README.md`** in the same commit: any change to behaviour, settings,
   files on the Pi, commands, API, hardware, wiring or installation must be
   reflected in the matching section. The README must always describe the
   current state of `main`.
2. **Add an entry to `CHANGELOG.md`** under `[Unreleased]` (Added / Changed / Fixed / Removed).
3. **Run the tests:** `python3 -m unittest discover -s tests -v` (Pi) and
   `cd esp32-c3 && pio test -e native` (ESP32-C3). Add tests for new logic on both.
4. **Keep `install.sh` safe to re-run:** it must only install what's missing,
   never overwrite user settings in `/etc/stompbox/` (`config.json`, `pedal.json`,
   `hotspot.conf`), and migrate older setups instead of breaking them.

## Releases

- Bump `VERSION` in `software/stompboxd.py`, move `[Unreleased]` entries to a new
  version heading in `CHANGELOG.md`, and update "Current version" at the top of the README.

## Hardware

- Edit the `.scad` source, then regenerate **all three** STL files of that variant
  (`base.stl`, `lid.stl`, `lid_no_labels.stl`) and `preview.png` – commands in the README.
- Keep every case variant within 170 mm in length.
- If footswitch pins change, update `hardware/wiring/` (diagram) and the wiring table in the README.

## Two platforms: Raspberry Pi (`software/`) and ESP32-C3 (`esp32-c3/`)

- The settings page `software/web/index.html` is **shared**: the ESP32-C3 build embeds it.
  Any page change must keep working on both, and the ESP32-C3 release binaries in
  `esp32-c3/release/` must be rebuilt (`cd esp32-c3 && pio run`) in the same commit.
- Keep the JSON API identical on both (`stompboxd.py` `Handler` and `esp32-c3/src/net.cpp`).
  New features: implement on both, or document in both READMEs which platform has them.
- Behaviour changes to the footswitch engine must be made in both `stompboxd.py` and `esp32-c3/src/engine.cpp`.
- ESP32-C3 changes update `esp32-c3/README.md` (and the comparison table there).
- Bump `STOMPBOX_VERSION` in `esp32-c3/include/config.h` and `version` in `esp32-c3/flasher/manifest.json` with each release.

## Pi Zero W constraints to remember

- One Wi-Fi radio: hotspot (`uap0`) and normal Wi-Fi (`wlan0`) must share a channel.
- Slow CPU: avoid frequent subprocess calls (cache `nmcli` results), keep polling light.
- 32-bit Raspberry Pi OS Trixie only (`aseqsend` needs alsa-utils ≥ 1.2.11).
- Test on real hardware before tagging a release; note in the README what was not tested.
