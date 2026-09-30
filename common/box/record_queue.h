/*
 * record_queue.h - money records waiting for the main unit's "ok"
 *
 * Every coin (Sub Vendo) or charging sale (Charging Station) is written
 * here BEFORE it is sent, and removed only after the main unit's signed
 * acknowledgement. WiFi drops, main unit reboots and our own power cuts
 * therefore can't lose one; resends are harmless because each record
 * carries a sequence number the main unit dedupes by.
 * LittleFS file: {"lastSeq": N, "items": [{"s","p","r","o","m"}]}
 *   s = seq, p = peso, r = reservation id (sub vendo), o = port and
 *   m = minutes (charging).
 */
#ifndef RECORD_QUEUE_H
#define RECORD_QUEUE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

struct BoxRecord {
  uint32_t seq;
  uint32_t peso;
  uint32_t rid;       // reservation open when a coin went in (sub vendo; 0 = none)
  uint8_t port;       // charging port 1-4 (0 = unclaimed / overflow)
  uint32_t minutes;   // charging minutes given
};

class RecordQueue {
public:
  RecordQueue(const char* file, uint8_t capacity) : _file(file), _capacity(capacity) {}

  void load() {
    _items.clear();
    File f = LittleFS.open(_file, "r");
    if (!f) return;
    DynamicJsonDocument d(docSize());
    bool bad = (bool)deserializeJson(d, f);
    f.close();
    if (bad) return;
    _lastSeq = d["lastSeq"] | 0UL;
    for (JsonObjectConst c : d["items"].as<JsonArrayConst>()) {
      if (_items.size() >= _capacity) break;
      _items.push_back({(uint32_t)(c["s"] | 0UL), (uint32_t)(c["p"] | 0UL), (uint32_t)(c["r"] | 0UL),
                        (uint8_t)(c["o"] | 0), (uint32_t)(c["m"] | 0UL)});
    }
  }

  size_t size() const { return _items.size(); }
  uint8_t capacity() const { return _capacity; }
  bool empty() const { return _items.empty(); }
  bool full() const { return _items.size() >= _capacity; }
  const BoxRecord& front() const { return _items.front(); }

  bool push(uint32_t peso, uint32_t rid, uint8_t port = 0, uint32_t minutes = 0) {
    if (full()) return false;
    _items.push_back({++_lastSeq, peso, rid, port, minutes});
    save();
    return true;
  }

  void pop() {
    if (_items.empty()) return;
    _items.erase(_items.begin());
    save();
  }

  // After (re)pairing: the main unit may have seen higher sequence numbers
  // (e.g. this board was erased) - continue above them or new records
  // would be ignored as duplicates.
  void raiseSeq(uint32_t atLeast) {
    if (atLeast <= _lastSeq) return;
    uint32_t next = atLeast;
    for (auto& c : _items) c.seq = ++next;   // queued records get fresh numbers too
    _lastSeq = next;
    save();
  }

private:
  const char* _file;
  uint8_t _capacity;
  std::vector<BoxRecord> _items;
  uint32_t _lastSeq = 0;

  size_t docSize() const { return 256 + (size_t)_capacity * 96; }

  void save() {
    DynamicJsonDocument d(docSize());
    d["lastSeq"] = _lastSeq;
    JsonArray arr = d.createNestedArray("items");
    for (auto& c : _items) {
      JsonObject o = arr.createNestedObject();
      o["s"] = c.seq;
      o["p"] = c.peso;
      if (c.rid) o["r"] = c.rid;
      if (c.port) o["o"] = c.port;
      if (c.minutes) o["m"] = c.minutes;
    }
    String tmp = String(_file) + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f) return;
    serializeJson(d, f);
    f.close();
    LittleFS.remove(_file);
    LittleFS.rename(tmp, _file);
  }
};

#endif // RECORD_QUEUE_H
