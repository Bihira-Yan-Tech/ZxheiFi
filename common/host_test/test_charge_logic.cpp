// Host tests for charging/charge_logic.h (the Charging Station's brain -
// pure C++, time injected, so every rule is checked here without hardware).
// Run: python tools/run_host_tests.py
#include <cstdio>
#include <cstdint>
#include "../../charging/charge_logic.h"

using zxc::ChargeLogic;
using zxc::ChargeRate;
using zxc::ChargeSale;

static int fails = 0;

#define CHECK(name, cond)                                                   \
  do {                                                                      \
    bool ok_ = (cond);                                                      \
    if (!ok_) fails++;                                                      \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name);                         \
  } while (0)

static const ChargeRate RATES[] = {{5, 30}, {10, 60}, {20, 150}};
static const uint32_t MIN = 60000;

static ChargeLogic fresh(uint8_t ports = 4, uint32_t maxMinutes = 180) {
  ChargeLogic c;
  c.configure(ports, RATES, 3, maxMinutes);
  c.setEnabled(true);
  return c;
}

int main() {
  // ---- rates ------------------------------------------------------------
  {
    ChargeLogic c = fresh();
    CHECK("exact rate P10 = 60 min", c.minutesFor(10) == 60);
    CHECK("exact rate P20 = 150 min", c.minutesFor(20) == 150);
    CHECK("fallback P7 = 7 x 30/5 = 42 min", c.minutesFor(7) == 42);
    CHECK("fallback P1 = 6 min", c.minutesFor(1) == 6);
    ChargeLogic none;
    none.configure(4, nullptr, 0, 180);
    none.setEnabled(true);
    CHECK("no rates -> 0 min", none.minutesFor(10) == 0);
    none.press(1, 1000, nullptr, nullptr);
    CHECK("no rates -> acceptor stays off", !none.acceptorOn());
  }

  // ---- press + coin + countdown ------------------------------------------
  {
    ChargeLogic c = fresh();
    CHECK("idle: nothing selected, acceptor off", c.selected() == 0 && !c.acceptorOn());
    c.press(2, 1000, nullptr, nullptr);
    CHECK("press selects the port and opens the acceptor", c.selected() == 2 && c.acceptorOn());
    ChargeSale s{};
    bool credited = c.coin(10, 2000, s);
    CHECK("coin credited to the selected port", credited && s.port == 2 && s.peso == 10 && s.minutes == 60);
    CHECK("port on with 60 min", c.portOn(2) && c.remainingMs(2) == 60 * MIN);
    CHECK("other ports untouched", !c.portOn(1) && !c.portOn(3) && !c.portOn(4));
    c.tick(2000 + 61000, nullptr, nullptr);
    CHECK("countdown: 61 s later", c.remainingMs(2) == 60 * MIN - 61000);
    c.tick(2000 + 60 * MIN + 1, nullptr, nullptr);
    CHECK("timeout: port off at 0", !c.portOn(2) && c.remainingMs(2) == 0);
  }

  // ---- selection window --------------------------------------------------
  {
    ChargeLogic c = fresh();
    c.press(1, 0, nullptr, nullptr);
    c.tick(19000, nullptr, nullptr);
    CHECK("window still open at 19 s", c.selected() == 1);
    ChargeSale s{};
    c.coin(5, 19000, s);
    c.tick(19000 + 19999, nullptr, nullptr);
    CHECK("a coin restarts the 20 s window", c.selected() == 1 && c.acceptorOn());
    c.tick(19000 + 20001, nullptr, nullptr);
    CHECK("window closes 20 s after the last activity", c.selected() == 0 && !c.acceptorOn());
    CHECK("port keeps running after the window closes", c.portOn(1));
  }

  // ---- top-up + switching ports ------------------------------------------
  {
    ChargeLogic c = fresh();
    ChargeSale s{};
    c.press(3, 0, nullptr, nullptr);
    c.coin(10, 100, s);
    c.tick(30 * MIN, nullptr, nullptr);
    c.press(3, 30 * MIN, nullptr, nullptr);
    c.coin(5, 30 * MIN + 100, s);
    CHECK("top-up adds to the running time", c.remainingMs(3) == 60 * MIN);
    c.press(4, 30 * MIN + 200, nullptr, nullptr);
    CHECK("pressing another port switches the selection", c.selected() == 4);
  }

  // ---- cap ---------------------------------------------------------------
  {
    ChargeLogic c = fresh(4, 180);
    ChargeSale s{};
    c.press(1, 0, nullptr, nullptr);
    c.coin(20, 0, s);   // 150
    CHECK("below cap: acceptor on", c.acceptorOn() && !c.atCap(1));
    c.coin(10, 0, s);   // 210 > 180
    CHECK("at/over cap: acceptor off", c.atCap(1) && !c.acceptorOn());
    CHECK("in-flight coin still credited (money never refused)", c.remainingMs(1) == 210 * MIN);
    c.press(1, 3, nullptr, nullptr);
    CHECK("pressing a capped port does not open the acceptor", !c.acceptorOn());
  }

  // ---- held coin ---------------------------------------------------------
  {
    ChargeLogic c = fresh();
    ChargeSale s{};
    bool credited = c.coin(10, 1000, s);
    CHECK("coin with no port selected is held", !credited && c.heldPeso() == 10);
    ChargeSale claimed{};
    bool hasClaim = false;
    c.press(2, 30000, &claimed, &hasClaim);
    CHECK("held coin claimed by the next press", hasClaim && claimed.port == 2 && claimed.peso == 10 &&
          claimed.minutes == 60 && c.remainingMs(2) == 60 * MIN && c.heldPeso() == 0);

    ChargeLogic d = fresh();
    d.coin(5, 1000, s);
    ChargeSale expired{};
    bool hasExpired = false;
    d.tick(1000 + 59999, &expired, &hasExpired);
    CHECK("held coin kept for 60 s", !hasExpired && d.heldPeso() == 5);
    d.tick(1000 + 60000, &expired, &hasExpired);
    CHECK("held coin expires as a port-0 sale", hasExpired && expired.port == 0 && expired.peso == 5 &&
          expired.minutes == 0 && d.heldPeso() == 0);
  }

  // ---- stop --------------------------------------------------------------
  {
    ChargeLogic c = fresh();
    ChargeSale s{};
    c.press(2, 0, nullptr, nullptr);
    c.coin(10, 0, s);
    c.tick(10 * MIN, nullptr, nullptr);
    uint32_t forfeited = c.stop(2);
    CHECK("stop returns the forfeited time", forfeited == 50 * MIN);
    CHECK("stop turns the port off", !c.portOn(2));
    CHECK("stop on an idle port returns 0", c.stop(1) == 0);
    CHECK("stop on an invalid port returns 0", c.stop(9) == 0);
  }

  // ---- snapshot / restore ------------------------------------------------
  {
    ChargeLogic c = fresh();
    ChargeSale s{};
    c.press(1, 0, nullptr, nullptr);
    c.coin(10, 0, s);
    c.press(4, 0, nullptr, nullptr);
    c.coin(5, 0, s);
    c.tick(90500, nullptr, nullptr);
    uint32_t snap[4];
    c.snapshot(snap);
    CHECK("snapshot rounds up to whole seconds (customer-favourable)",
          snap[0] == 3510 && snap[1] == 0 && snap[2] == 0 && snap[3] == 1710);
    ChargeLogic r = fresh();
    r.restore(snap, 555);
    CHECK("restore resumes each port", r.remainingMs(1) == 3510000UL && r.remainingMs(4) == 1710000UL && !r.portOn(2));
    r.tick(555 + 1000, nullptr, nullptr);
    CHECK("restored ports count down from the restore time", r.remainingMs(1) == 3509000UL);
  }

  // ---- disabled (not paired / removed) -----------------------------------
  {
    ChargeLogic c = fresh();
    ChargeSale s{};
    c.press(1, 0, nullptr, nullptr);
    c.coin(10, 0, s);
    c.setEnabled(false);
    CHECK("disabled: acceptor off", !c.acceptorOn());
    c.press(2, 10, nullptr, nullptr);
    CHECK("disabled: press does not open the acceptor", !c.acceptorOn());
    c.tick(60000, nullptr, nullptr);
    CHECK("disabled: running ports keep counting down", c.remainingMs(1) == 59 * MIN);
  }

  // ---- port count --------------------------------------------------------
  {
    ChargeLogic c = fresh(2);
    c.press(3, 0, nullptr, nullptr);
    CHECK("ports=2: port 3 ignored", c.selected() == 0 && c.ports() == 2);
    c.press(0, 0, nullptr, nullptr);
    CHECK("port 0 ignored", c.selected() == 0);
  }

  // ---- millis() wrap-around ----------------------------------------------
  {
    ChargeLogic c = fresh();
    ChargeSale s{};
    uint32_t t0 = 0xFFFFFFFFu - 5000;
    c.tick(t0, nullptr, nullptr);
    c.press(1, t0, nullptr, nullptr);
    c.coin(10, t0, s);
    c.tick(t0 + 10000, nullptr, nullptr);   // wraps past 0
    CHECK("wrap-around: counts 10 s correctly", c.remainingMs(1) == 60 * MIN - 10000);
    CHECK("wrap-around: window still open", c.selected() == 1);
  }

  printf("CHARGE LOGIC %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
