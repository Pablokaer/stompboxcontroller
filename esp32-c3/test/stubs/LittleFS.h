// LittleFS stand-in for unit tests: files live in ./.test_fs/
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>

class File {
 public:
  FILE* f = nullptr;
  explicit operator bool() const { return f != nullptr; }
  int read() { return f ? fgetc(f) : -1; }
  size_t readBytes(char* b, size_t n) { return f ? fread(b, 1, n, f) : 0; }
  size_t write(uint8_t c) { return f ? fwrite(&c, 1, 1, f) : 0; }
  size_t write(const uint8_t* b, size_t n) { return f ? fwrite(b, 1, n, f) : 0; }
  void close() { if (f) fclose(f); f = nullptr; }
};

struct FakeFS {
  static std::string p(const std::string& path) { mkdir(".test_fs", 0755); return ".test_fs" + path; }
  bool begin(bool = false) { return true; }
  File open(const std::string& path, const char* mode) { File x; x.f = fopen(p(path).c_str(), *mode == 'w' ? "wb" : "rb"); return x; }
  bool remove(const std::string& path) { return ::remove(p(path).c_str()) == 0; }
  bool rename(const std::string& a, const std::string& b) { return ::rename(p(a).c_str(), p(b).c_str()) == 0; }
};
extern FakeFS LittleFS;
