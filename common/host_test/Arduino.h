// Minimal Arduino shim so common/zx_protocol.h (including its String
// helpers) compiles and runs on the PC - see test_zx_protocol.cpp.
#pragma once
#include <string>
#include <cstring>
#include <cstdint>

class String {
public:
  String() {}
  String(const char* s) : _s(s ? s : "") {}
  String(const std::string& s) : _s(s) {}
  String(char c) : _s(1, c) {}
  String(unsigned char v) : _s(std::to_string((unsigned)v)) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned v) : _s(std::to_string(v)) {}
  size_t length() const { return _s.size(); }
  const char* c_str() const { return _s.c_str(); }
  char operator[](size_t i) const { return _s[i]; }
  String& operator+=(char c) { _s += c; return *this; }
  String& operator+=(const String& o) { _s += o._s; return *this; }
  String substring(size_t from) const { return from >= _s.size() ? String() : String(_s.substr(from)); }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* o) const { return _s == o; }
  friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
  friend String operator+(const String& a, const char* b) { return String(a._s + b); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a) + b._s); }
private:
  std::string _s;
};
