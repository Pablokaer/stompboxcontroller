# Rules for changing this project

Applies to everyone – people and AI assistants (Claude, Copilot, …).

## Every change

1. **Update `README.md`** in the same commit: any change to behaviour, settings,
   files on the Pi, commands, API, hardware, wiring or installation must be
   reflected in the matching section. The README must always describe the
   current state of `main`.
2. **Add an entry to `CHANGELOG.md`** under `[Unreleased]` (Added / Changed / Fixed / Removed).
3. **Run the tests:** `python3 -m unittest discover -s tests -v`. Add tests for new button logic.
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

## Pi Zero W constraints to remember

- One Wi-Fi radio: hotspot (`uap0`) and normal Wi-Fi (`wlan0`) must share a channel.
- Slow CPU: avoid frequent subprocess calls (cache `nmcli` results), keep polling light.
- 32-bit Raspberry Pi OS Trixie only (`aseqsend` needs alsa-utils ≥ 1.2.11).
- Test on real hardware before tagging a release; note in the README what was not tested.
