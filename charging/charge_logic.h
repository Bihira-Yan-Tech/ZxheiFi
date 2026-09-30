/*
 * charge_logic.h - the Charging Station's rules, with no hardware in it
 * ZXHEIFI Charging Station firmware (v2)
 *
 * Pure C++ (no Arduino calls) and time is passed in, so every rule is
 * tested on the PC: common/host_test/test_charge_logic.cpp.
 *
 *   press(port)  customer pressed a port's button: that port is selected
 *                for WINDOW_MS and the coin acceptor may take coins
 *   coin(peso)   a coin went in: the selected port gets its minutes (the
 *                exact Charging Rate, or the lowest-peso rate pro rata -
 *                a coin is never wasted). No port selected = held for
 *                HELD_MS for the next press, then reported as a port-0 sale
 *   tick(now)    counts every port down; closes the window after
 *                WINDOW_MS without activity; expires a held coin
 *
 * A port can't be topped up past maxMinutes (the acceptor turns off), but
 * a coin that's already on its way is always credited. Ports are 1-based;
 * the sales these calls report go to the main unit (charging.ino queues
 * them). millis() wrap-around is handled by unsigned arithmetic.
 */
#ifndef CHARGE_LOGIC_H
#define CHARGE_LOGIC_H

#include <stdint.h>

namespace zxc {

struct ChargeRate {
  uint32_t peso;
  uint32_t minutes;
};

struct ChargeSale {
  uint8_t port;       // 1..4, or 0 = a coin nobody claimed
  uint32_t peso;
  uint32_t minutes;
};

class ChargeLogic {
public:
  static const uint8_t MAX_PORTS = 4;
  static const uint8_t MAX_RATES = 10;
  static const uint32_t WINDOW_MS = 20000;   // selection stays open this long after the last press/coin
  static const uint32_t HELD_MS = 60000;     // a coin with no port selected waits this long
  static const uint32_t WARN_MS = 60000;     // "almost done" blink on the screen
  static const uint32_t MINUTE_MS = 60000;

  void configure(uint8_t ports, const ChargeRate* rates, uint8_t nRates, uint32_t maxMinutes) {
    _ports = ports > MAX_PORTS ? MAX_PORTS : ports;
    _nRates = 0;
    for (uint8_t i = 0; rates && i < nRates && _nRates < MAX_RATES; i++) {
      if (rates[i].peso && rates[i].minutes) _rates[_nRates++] = rates[i];
    }
    _maxMs = maxMinutes * MINUTE_MS;
    if (_selected > _ports) _selected = 0;
  }

  // Off while unpaired / removed / without rates: no new coins, but ports
  // that are already charging keep counting down.
  void setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) _selected = 0;
  }

  uint8_t ports() const { return _ports; }
  bool enabled() const { return _enabled; }
  bool hasRates() const { return _nRates > 0; }
  uint8_t selected() const { return _selected; }
  uint32_t heldPeso() const { return _heldPeso; }
  uint32_t windowPeso() const { return _windowPeso; }         // ₱ inserted since the port was pressed
  uint32_t windowMinutes() const { return _windowMinutes; }

  uint32_t remainingMs(uint8_t port) const { return valid(port) ? _remaining[port - 1] : 0; }
  bool portOn(uint8_t port) const { return remainingMs(port) > 0; }
  bool warn(uint8_t port) const { return portOn(port) && remainingMs(port) <= WARN_MS; }
  bool atCap(uint8_t port) const { return valid(port) && _maxMs && _remaining[port - 1] >= _maxMs; }

  bool acceptorOn() const {
    return _enabled && hasRates() && _selected && !atCap(_selected);
  }

  // Seconds left in the selection window (for the screen), 0 if closed.
  uint32_t windowLeftSec(uint32_t nowMs) const {
    if (!_selected) return 0;
    uint32_t idle = nowMs - _lastActivity;
    return idle >= WINDOW_MS ? 0 : (WINDOW_MS - idle + 999) / 1000;
  }

  uint32_t minutesFor(uint32_t peso) const {
    if (!_nRates || !peso) return 0;
    const ChargeRate* lowest = &_rates[0];
    for (uint8_t i = 0; i < _nRates; i++) {
      if (_rates[i].peso == peso) return _rates[i].minutes;
      if (_rates[i].peso < lowest->peso) lowest = &_rates[i];
    }
    return (uint32_t)((uint64_t)peso * lowest->minutes / lowest->peso);
  }

  void press(uint8_t port, uint32_t nowMs, ChargeSale* claimed, bool* hasClaim) {
    if (hasClaim) *hasClaim = false;
    advance(nowMs);
    if (!valid(port) || !_enabled || !hasRates()) return;
    if (_selected != port) {
      _windowPeso = 0;
      _windowMinutes = 0;
    }
    _selected = port;
    _lastActivity = nowMs;
    if (_heldPeso) {                        // a coin that went in before anyone pressed
      ChargeSale s = credit(port, _heldPeso);
      _heldPeso = 0;
      if (claimed) *claimed = s;
      if (hasClaim) *hasClaim = true;
    }
  }

  // True when the coin went to a port (sale filled in); false when held.
  bool coin(uint32_t peso, uint32_t nowMs, ChargeSale& sale) {
    advance(nowMs);
    if (!peso) return false;
    if (_selected) {
      sale = credit(_selected, peso);
      _lastActivity = nowMs;
      return true;
    }
    if (!_heldPeso) _heldAt = nowMs;
    _heldPeso += peso;
    return false;
  }

  void tick(uint32_t nowMs, ChargeSale* expired, bool* hasExpired) {
    if (hasExpired) *hasExpired = false;
    advance(nowMs);
    if (_selected && nowMs - _lastActivity >= WINDOW_MS) _selected = 0;
    if (_heldPeso && nowMs - _heldAt >= HELD_MS) {
      if (expired) *expired = ChargeSale{0, _heldPeso, 0};
      if (hasExpired) *hasExpired = true;
      _heldPeso = 0;
    }
  }

  // Admin > Stop: the port turns off; returns the time the customer loses.
  uint32_t stop(uint8_t port) {
    if (!valid(port)) return 0;
    uint32_t left = _remaining[port - 1];
    _remaining[port - 1] = 0;
    return left;
  }

  // Seconds left per port, rounded UP - a power cut never costs the
  // customer time.
  void snapshot(uint32_t outSec[MAX_PORTS]) const {
    for (uint8_t i = 0; i < MAX_PORTS; i++) outSec[i] = (_remaining[i] + 999) / 1000;
  }

  void restore(const uint32_t inSec[MAX_PORTS], uint32_t nowMs) {
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
      uint32_t sec = inSec[i] > 86400u * 2 ? 86400u * 2 : inSec[i];   // sanity cap: 2 days
      _remaining[i] = sec * 1000;
    }
    _lastMs = nowMs;
    _clockStarted = true;
  }

private:
  uint8_t _ports = MAX_PORTS;
  ChargeRate _rates[MAX_RATES];
  uint8_t _nRates = 0;
  uint32_t _maxMs = 0;
  bool _enabled = false;
  uint32_t _remaining[MAX_PORTS] = {0, 0, 0, 0};
  uint8_t _selected = 0;
  uint32_t _lastActivity = 0;
  uint32_t _heldPeso = 0, _heldAt = 0;
  uint32_t _windowPeso = 0, _windowMinutes = 0;
  uint32_t _lastMs = 0;
  bool _clockStarted = false;

  bool valid(uint8_t port) const { return port >= 1 && port <= _ports; }

  void advance(uint32_t nowMs) {
    if (!_clockStarted) {
      _lastMs = nowMs;
      _clockStarted = true;
      return;
    }
    uint32_t elapsed = nowMs - _lastMs;
    _lastMs = nowMs;
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
      _remaining[i] = _remaining[i] > elapsed ? _remaining[i] - elapsed : 0;
    }
  }

  ChargeSale credit(uint8_t port, uint32_t peso) {
    uint32_t minutes = minutesFor(peso);
    _remaining[port - 1] += minutes * MINUTE_MS;
    if (port == _selected) {
      _windowPeso += peso;
      _windowMinutes += minutes;
    }
    return ChargeSale{port, peso, minutes};
  }
};

} // namespace zxc

#endif // CHARGE_LOGIC_H
