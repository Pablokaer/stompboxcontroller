# Changelog

All notable changes to this project. Newest first.
Format: [Keep a Changelog](https://keepachangelog.com/), versions follow `VERSION` in `software/stompboxd.py`.

## [Unreleased]

_Nothing yet._

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
