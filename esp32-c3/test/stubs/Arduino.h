// Minimal Arduino / FreeRTOS stand-ins so the firmware logic (settings.cpp,
// engine.cpp) compiles and runs on a computer for unit tests (pio test -e native).
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

class String : public std::string {
 public:
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  unsigned length() const { return (unsigned)size(); }
  int toInt() const { return atoi(c_str()); }
  bool concat(const char* s) { append(s); return true; }
  void toUpperCase() { for (auto& c : *this) c = toupper(c); }
  void toLowerCase() { for (auto& c : *this) c = tolower(c); }
  void trim() {
    erase(0, find_first_not_of(" \t\r\n"));
    erase(find_last_not_of(" \t\r\n") + 1);
  }
};
class StringSumHelper : public String { using String::String; };

using std::max;
using std::min;
#define PROGMEM
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// Pins: tests set fakePinLevel[gpio] = LOW to "press" a real footswitch.
extern int fakePinLevel[64];
inline void pinMode(int, int) {}
inline int digitalRead(int pin) { return fakePinLevel[pin & 63]; }
inline void digitalWrite(int, int) {}

inline uint32_t millis() {
  static auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct FakeSerial {
  void begin(int) {}
  void println(const char* s) { if (getenv("STOMPBOX_TEST_LOG")) printf("%s\n", s); }
  void println(const String& s) { println(s.c_str()); }
};
extern FakeSerial Serial;

// FreeRTOS
typedef void* TaskHandle_t;
inline int xTaskCreate(void (*fn)(void*), const char*, int, void* arg, int, TaskHandle_t*) {
  std::thread(fn, arg).detach();
  return 1;
}
inline void vTaskDelay(uint32_t ms) { delay(ms); }
#define pdMS_TO_TICKS(ms) (ms)
