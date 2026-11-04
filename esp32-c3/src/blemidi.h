// BLE-MIDI packet parser (Bluetooth LE MIDI spec): header byte, then for each
// message an optional timestamp byte, a status byte (or running status) and data.
// Pure function – no Arduino dependencies, so it is unit-tested on the computer.
#pragma once
#include <stddef.h>
#include <stdint.h>

template <typename OnCC, typename OnProgram>
void parseBleMidi(const uint8_t* d, size_t n, OnCC onCC, OnProgram onProgram) {
  if (n < 3 || !(d[0] & 0x80)) return;
  uint8_t status = 0;
  size_t i = 1;
  while (i < n) {
    if (d[i] & 0x80) {                      // timestamp byte
      if (++i >= n) break;
      if (d[i] & 0x80) {
        status = d[i++];
        if (status == 0xF0) {                 // SysEx: skip to the end marker
          while (i < n && d[i] != 0xF7) i++;
          i++;
          status = 0;
          continue;
        }
      }
    }
    uint8_t type = status & 0xF0;
    size_t need = (type == 0xC0 || type == 0xD0) ? 1 : (status ? 2 : 0);
    if (!need || i + need > n) break;
    if (type == 0xB0) onCC(d[i] & 0x7F, d[i + 1] & 0x7F);
    else if (type == 0xC0) onProgram(d[i] & 0x7F);
    i += need;
  }
}
