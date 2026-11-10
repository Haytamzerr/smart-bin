// Unit tests for bin_logic.h, run on a PC:  g++ -std=c++11 -I smart_bin test/test_bin_logic.cpp -o t && ./t
#include <cstdio>
#include "bin_logic.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

void test_median_filter() {
  MedianFilter<5> f;
  CHECK(f.median() == -1);              // nothing yet
  f.add(20); f.add(21); f.add(400);     // one wild echo
  f.add(19); f.add(0); f.add(20);       // 0 = no echo, ignored
  CHECK(f.median() == 20);              // the spike does not move the median
  f.add(22); f.add(23); f.add(22);
  CHECK(f.median() == 22);              // follows the new distance once it is stable
}

void test_lid_opens_and_closes_after_hold() {
  LidController lid(25, 3000, 1500);
  CHECK(!lid.update(-1, false, 0));                 // nothing in front
  CHECK(lid.update(15, false, 100));                // hand: opens
  CHECK(lid.state() == LidController::OPEN);
  CHECK(!lid.update(-1, false, 2000));              // hand gone 1.9 s: still open
  CHECK(lid.update(-1, false, 3100));               // 3 s after the hand: closes
  CHECK(lid.state() == LidController::CLOSED);
  CHECK(lid.opens() == 1);
}

void test_hand_kept_near_keeps_lid_open() {
  LidController lid(25, 3000, 1500);
  lid.update(10, false, 0);
  for (uint32_t t = 500; t <= 10000; t += 500) lid.update(10, false, t);  // still throwing things
  CHECK(lid.state() == LidController::OPEN);
  lid.update(-1, false, 12000);
  CHECK(lid.state() == LidController::OPEN);        // 2 s since the hand
  lid.update(-1, false, 13000);
  CHECK(lid.state() == LidController::CLOSED);
}

void test_far_object_does_not_open() {
  LidController lid(25, 3000, 1500);
  lid.update(60, false, 0);                         // someone walking past at 60 cm
  CHECK(lid.state() == LidController::CLOSED);
}

void test_full_bin_stays_closed() {
  LidController lid(25, 3000, 1500);
  CHECK(!lid.update(10, true, 0));
  CHECK(lid.state() == LidController::CLOSED);
  CHECK(lid.opens() == 0);
}

void test_millis_overflow() {
  // millis() wraps after ~49.7 days: unsigned subtraction must still work
  LidController lid(25, 3000, 1500);
  uint32_t near_end = 0xFFFFFFFFu - 1000;
  lid.update(10, false, near_end);
  CHECK(lid.state() == LidController::OPEN);
  lid.update(-1, false, near_end + 500);
  lid.update(-1, false, 2600);                      // wrapped around: 4.1 s later
  CHECK(lid.state() == LidController::CLOSED);
}

void test_fill_percent() {
  CHECK(FillGauge::percentFor(40, 40, 8) == 0);
  CHECK(FillGauge::percentFor(50, 40, 8) == 0);     // further than the bottom: empty
  CHECK(FillGauge::percentFor(24, 40, 8) == 50);
  CHECK(FillGauge::percentFor(8, 40, 8) == 100);
  CHECK(FillGauge::percentFor(3, 40, 8) == 100);
  CHECK(FillGauge::percentFor(-1, 40, 8) == -1);
}

void test_fill_levels_with_hysteresis() {
  FillGauge g(40, 8);
  CHECK(g.update(35) == FillGauge::LOW_LEVEL);      // 15 %
  CHECK(g.update(24) == FillGauge::MEDIUM_LEVEL);   // 50 %
  CHECK(g.update(25) == FillGauge::MEDIUM_LEVEL);   // 46 %: stays medium (hysteresis)
  CHECK(g.update(26) == FillGauge::LOW_LEVEL);      // 43 %: back to low
  CHECK(g.update(14) == FillGauge::HIGH_LEVEL);     // 81 %
  CHECK(g.update(9) == FillGauge::FULL_LEVEL);      // 96 %
  CHECK(g.full());
  CHECK(g.update(-1) == FillGauge::FULL_LEVEL);     // lost echo: keep the last level
  CHECK(g.update(40) == FillGauge::LOW_LEVEL);      // emptied
  CHECK(!g.full());
}

int main() {
  test_median_filter();
  test_lid_opens_and_closes_after_hold();
  test_hand_kept_near_keeps_lid_open();
  test_far_object_does_not_open();
  test_full_bin_stays_closed();
  test_millis_overflow();
  test_fill_percent();
  test_fill_levels_with_hysteresis();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
