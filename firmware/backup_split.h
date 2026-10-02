/*
 * backup_split.h - splits an uploaded backup into its files, streaming
 * ZXHEIFI - main unit firmware (v2)
 *
 * A backup is one JSON object:
 *   {"zxheifiBackup":1,"fw":"...","board":"...","createdAt":N,
 *    "files":{"config.json":{...},"vouchers.json":[...],...}}
 * An ESP8266 can't hold a whole backup in RAM, so this walks the text one
 * byte at a time (in chunks of any size) and hands each file's raw JSON to
 * a Sink - which backup_api.h writes to a temp file. Strings (with escaped
 * quotes and braces inside), nesting and whitespace are handled; unknown
 * top-level keys are skipped. finish() says whether the WHOLE thing was a
 * well-formed version-1 backup - nothing is installed unless it was.
 *
 * Pure C++ (no Arduino), tested on the PC: common/host_test/test_backup_split.cpp
 */
#ifndef BACKUP_SPLIT_H
#define BACKUP_SPLIT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace zxb {

class BackupSplitter {
public:
  enum State { Running, Ok, NotABackup, Malformed };

  struct Sink {
    virtual bool fileStart(const char* name) = 0;   // false = refuse this file (whole backup fails)
    virtual void fileData(const char* p, size_t n) = 0;
    virtual bool fileEnd() = 0;
    virtual ~Sink() {}
  };

  static const size_t MAX_NAME = 32;

  explicit BackupSplitter(Sink& sink) : _sink(sink) {}

  void feed(const char* p, size_t n) {
    for (size_t i = 0; i < n && _result == Running; i++) step(p[i]);
    flush();
  }

  State finish() {
    flush();
    if (_result != Running) return _result;
    if (_st != TopDone) return _result = Malformed;
    if (!_sawVersion || _version != 1) return _result = NotABackup;
    return _result = Ok;
  }

  State state() const { return _result; }
  long version() const { return _version; }
  const char* board() const { return _board; }
  const char* fw() const { return _fw; }
  uint16_t fileCount() const { return _files; }

private:
  enum St {
    Start, TopKeyOrEnd, TopKey, TopColon, TopValue, TopAfterValue, TopDone,
    VersionNum, CaptureStr, SkipStr, SkipNested, SkipScalar,
    FilesKeyOrEnd, FileKey, FileColon, FileValueStart, FileValue, FilesAfterValue
  };

  Sink& _sink;
  State _result = Running;
  St _st = Start;
  char _key[MAX_NAME + 1];
  size_t _keyLen = 0;
  bool _esc = false, _inStr = false;
  int _depth = 0;
  long _version = 0;
  bool _sawVersion = false, _versionNeg = false, _versionDigits = false;
  char _board[17] = {0};
  char _fw[17] = {0};
  char* _cap = nullptr;
  size_t _capLen = 0;
  uint16_t _files = 0;
  char _out[128];
  size_t _outLen = 0;

  static bool ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

  void fail() { _result = Malformed; }

  void flush() {
    if (_outLen) {
      _sink.fileData(_out, _outLen);
      _outLen = 0;
    }
  }

  void emit(char c) {
    _out[_outLen++] = c;
    if (_outLen == sizeof(_out)) flush();
  }

  // Reads a key's characters into _key; true when the closing quote came.
  bool keyChar(char c) {
    if (_esc) {
      _esc = false;
    } else if (c == '\\') {
      _esc = true;
      return false;
    } else if (c == '"') {
      _key[_keyLen] = 0;
      return true;
    }
    if (_keyLen >= MAX_NAME) {
      fail();
      return false;
    }
    _key[_keyLen++] = c;
    return false;
  }

  void startValue(char c) {
    if (!strcmp(_key, "files")) {
      if (c == '{') _st = FilesKeyOrEnd;
      else fail();
    } else if (!strcmp(_key, "zxheifiBackup")) {
      if (c == '-' || (c >= '0' && c <= '9')) {
        _sawVersion = true;
        _version = 0;
        _versionNeg = c == '-';
        _versionDigits = !_versionNeg;
        if (!_versionNeg) _version = c - '0';
        _st = VersionNum;
      } else {
        _st = SkipScalar;            // not a number: version stays unset
        if (c == '"') _st = SkipStr;
        else if (c == '{' || c == '[') { _depth = 1; _inStr = false; _st = SkipNested; }
      }
    } else if ((!strcmp(_key, "board") || !strcmp(_key, "fw")) && c == '"') {
      _cap = !strcmp(_key, "board") ? _board : _fw;
      _capLen = 0;
      _cap[0] = 0;
      _st = CaptureStr;
    } else if (c == '"') {
      _st = SkipStr;
    } else if (c == '{' || c == '[') {
      _depth = 1;
      _inStr = false;
      _st = SkipNested;
    } else {
      _st = SkipScalar;
    }
  }

  void step(char c) {
    switch (_st) {
      case Start:
        if (ws(c)) return;
        if (c == '{') _st = TopKeyOrEnd;
        else fail();
        return;
      case TopKeyOrEnd:
        if (ws(c)) return;
        if (c == '"') { _keyLen = 0; _esc = false; _st = TopKey; }
        else if (c == '}') _st = TopDone;
        else fail();
        return;
      case TopKey:
        if (keyChar(c)) _st = TopColon;
        return;
      case TopColon:
        if (ws(c)) return;
        if (c == ':') _st = TopValue;
        else fail();
        return;
      case TopValue:
        if (ws(c)) return;
        startValue(c);
        return;
      case TopAfterValue:
        if (ws(c)) return;
        if (c == ',') _st = TopKeyOrEnd;
        else if (c == '}') _st = TopDone;
        else fail();
        return;
      case TopDone:
        if (!ws(c)) fail();
        return;
      case VersionNum:
        if (c >= '0' && c <= '9') {
          _versionDigits = true;
          if (_version < 100000000L) _version = _version * 10 + (c - '0');
          return;
        }
        if (c == '.' || c == 'e' || c == 'E' || c == '+') { _sawVersion = false; _st = SkipScalar; return; }
        if (!_versionDigits) { fail(); return; }
        if (_versionNeg) _version = -_version;
        _st = TopAfterValue;
        step(c);
        return;
      case CaptureStr:
        if (_esc) { _esc = false; }
        else if (c == '\\') { _esc = true; return; }
        else if (c == '"') { _st = TopAfterValue; return; }
        if (_capLen < 16) { _cap[_capLen++] = c; _cap[_capLen] = 0; }
        return;
      case SkipStr:
        if (_esc) _esc = false;
        else if (c == '\\') _esc = true;
        else if (c == '"') _st = TopAfterValue;
        return;
      case SkipNested:
        if (_inStr) {
          if (_esc) _esc = false;
          else if (c == '\\') _esc = true;
          else if (c == '"') _inStr = false;
        } else if (c == '"') {
          _inStr = true;
        } else if (c == '{' || c == '[') {
          _depth++;
        } else if (c == '}' || c == ']') {
          if (--_depth == 0) _st = TopAfterValue;
        }
        return;
      case SkipScalar:
        if (c == ',' || c == '}' || ws(c)) { _st = TopAfterValue; step(c); }
        return;
      case FilesKeyOrEnd:
        if (ws(c)) return;
        if (c == '"') { _keyLen = 0; _esc = false; _st = FileKey; }
        else if (c == '}') _st = TopAfterValue;
        else fail();
        return;
      case FileKey:
        if (keyChar(c)) _st = FileColon;
        return;
      case FileColon:
        if (ws(c)) return;
        if (c == ':') _st = FileValueStart;
        else fail();
        return;
      case FileValueStart:
        if (ws(c)) return;
        if (c != '{' && c != '[') { fail(); return; }
        if (!_sink.fileStart(_key)) { fail(); return; }
        _files++;
        _depth = 1;
        _inStr = false;
        _esc = false;
        emit(c);
        _st = FileValue;
        return;
      case FileValue:
        emit(c);
        if (_inStr) {
          if (_esc) _esc = false;
          else if (c == '\\') _esc = true;
          else if (c == '"') _inStr = false;
        } else if (c == '"') {
          _inStr = true;
        } else if (c == '{' || c == '[') {
          _depth++;
        } else if (c == '}' || c == ']') {
          if (--_depth == 0) {
            flush();
            if (!_sink.fileEnd()) { fail(); return; }
            _st = FilesAfterValue;
          }
        }
        return;
      case FilesAfterValue:
        if (ws(c)) return;
        if (c == ',') _st = FilesKeyOrEnd;
        else if (c == '}') _st = TopAfterValue;
        else fail();
        return;
    }
  }
};

} // namespace zxb

#endif // BACKUP_SPLIT_H
