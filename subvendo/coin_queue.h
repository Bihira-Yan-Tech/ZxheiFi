/*
 * coin_queue.h - coins waiting for the main unit's acknowledgement
 * ZXHEIFI Sub Vendo firmware (v2)
 *
 * Every coin is written here BEFORE it is sent, and removed only after
 * the main unit's signed "ok". WiFi drops, main unit reboots and our own
 * power cuts therefore can't lose a coin; resends are harmless because
 * each coin carries a sequence number the main unit dedupes by.
 * LittleFS /queue.json: {"lastSeq": N, "items": [{"s": seq, "p": peso, "r": rid}]}
 */
#ifndef COIN_QUEUE_H
#define COIN_QUEUE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include "sub_config.h"
#include "zx_protocol.h"

struct QueuedCoin {
  uint32_t seq;
  uint32_t peso;
  uint32_t rid;    // reservation open when the coin went in (0 = none)
};

class CoinQueue {
public:
  void load() {
    _items.clear();
    File f = LittleFS.open(SUB_QUEUE_FILE, "r");
    if (!f) return;
    DynamicJsonDocument d(2048);
    bool bad = (bool)deserializeJson(d, f);
    f.close();
    if (bad) return;
    _lastSeq = d["lastSeq"] | 0UL;
    for (JsonObjectConst c : d["items"].as<JsonArrayConst>()) {
      if (_items.size() >= ZX_COIN_QUEUE_MAX) break;
      _items.push_back({(uint32_t)(c["s"] | 0UL), (uint32_t)(c["p"] | 0UL), (uint32_t)(c["r"] | 0UL)});
    }
  }

  size_t size() const { return _items.size(); }
  bool empty() const { return _items.empty(); }
  bool full() const { return _items.size() >= ZX_COIN_QUEUE_MAX; }
  const QueuedCoin& front() const { return _items.front(); }

  bool push(uint32_t peso, uint32_t rid) {
    if (full()) return false;
    _items.push_back({++_lastSeq, peso, rid});
    save();
    return true;
  }

  void pop() {
    if (_items.empty()) return;
    _items.erase(_items.begin());
    save();
  }

  // After (re)pairing: the main unit may have seen higher sequence numbers
  // (e.g. this board was erased) - continue above them or new coins would
  // be ignored as duplicates.
  void raiseSeq(uint32_t atLeast) {
    if (atLeast <= _lastSeq) return;
    uint32_t next = atLeast;
    for (auto& c : _items) c.seq = ++next;   // queued coins get fresh numbers too
    _lastSeq = next;
    save();
  }

private:
  std::vector<QueuedCoin> _items;
  uint32_t _lastSeq = 0;

  void save() {
    DynamicJsonDocument d(2048);
    d["lastSeq"] = _lastSeq;
    JsonArray arr = d.createNestedArray("items");
    for (auto& c : _items) {
      JsonObject o = arr.createNestedObject();
      o["s"] = c.seq;
      o["p"] = c.peso;
      o["r"] = c.rid;
    }
    File f = LittleFS.open(String(SUB_QUEUE_FILE) + ".tmp", "w");
    if (!f) return;
    serializeJson(d, f);
    f.close();
    LittleFS.remove(SUB_QUEUE_FILE);
    LittleFS.rename(String(SUB_QUEUE_FILE) + ".tmp", SUB_QUEUE_FILE);
  }
};

#endif // COIN_QUEUE_H
