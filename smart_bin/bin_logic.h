// Smart bin logic, written without any Arduino function so it can be unit-tested on a PC.
//
//   MedianFilter    removes the spikes an HC-SR04 sometimes returns (0 cm, 400 cm echoes)
//   LidController   decides when the lid opens and closes (state machine with timers)
//   FillGauge       turns the inside sensor's distance into a fill percentage and level
//
// Time is passed in milliseconds (millis() on the board, a fake clock in the tests).

#pragma once
#include <stdint.h>

// ---------- median of the last N readings ----------
template <uint8_t N>
class MedianFilter {
 public:
  // Add a reading (cm). Readings <= 0 mean "no echo" and are ignored.
  void add(int value) {
    if (value <= 0) return;
    values_[next_] = value;
    next_ = (next_ + 1) % N;
    if (count_ < N) count_++;
  }

  // Median of the stored readings, or -1 before the first valid one.
  int median() const {
    if (count_ == 0) return -1;
    int sorted[N];
    for (uint8_t i = 0; i < count_; i++) sorted[i] = values_[i];
    for (uint8_t i = 1; i < count_; i++) {  // insertion sort: N is tiny
      int v = sorted[i];
      int8_t j = i - 1;
      while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; j--; }
      sorted[j + 1] = v;
    }
    return sorted[count_ / 2];
  }

 private:
  int values_[N] = {0};
  uint8_t next_ = 0;
  uint8_t count_ = 0;
};

// ---------- lid ----------
//
//   CLOSED --hand seen--> OPEN --no hand for holdMs (and open >= minOpenMs)--> CLOSED
//                          ^ a hand seen again while open restarts the hold timer
//
// The lid never opens while the bin is full (it would overflow).
class LidController {
 public:
  enum State : uint8_t { CLOSED, OPEN };

  LidController(int triggerCm, uint32_t holdMs, uint32_t minOpenMs)
      : triggerCm_(triggerCm), holdMs_(holdMs), minOpenMs_(minOpenMs) {}

  // distanceCm: filtered front distance (-1 = nothing seen). Returns true when the state changed.
  bool update(int distanceCm, bool binFull, uint32_t now) {
    bool hand = distanceCm > 0 && distanceCm <= triggerCm_;
    if (hand) lastHand_ = now;
    if (state_ == CLOSED && hand && !binFull) {
      state_ = OPEN;
      openedAt_ = now;
      opens_++;
      return true;
    }
    if (state_ == OPEN && now - lastHand_ >= holdMs_ && now - openedAt_ >= minOpenMs_) {
      state_ = CLOSED;
      return true;
    }
    return false;
  }

  State state() const { return state_; }
  uint32_t opens() const { return opens_; }  // how many times the bin was used

 private:
  int triggerCm_;
  uint32_t holdMs_, minOpenMs_;
  State state_ = CLOSED;
  uint32_t lastHand_ = 0, openedAt_ = 0, opens_ = 0;
};

// ---------- fill level ----------
//
// The inside sensor looks down from the lid: the closer the rubbish, the fuller the bin.
//   emptyCm: distance to the bottom of the empty bin, fullCm: distance when it is full.
// Levels use a 5 % hysteresis so the LED does not flicker around a threshold.
class FillGauge {
 public:
  enum Level : uint8_t { LOW_LEVEL, MEDIUM_LEVEL, HIGH_LEVEL, FULL_LEVEL };

  FillGauge(int emptyCm, int fullCm) : emptyCm_(emptyCm), fullCm_(fullCm) {}

  static int percentFor(int distanceCm, int emptyCm, int fullCm) {
    if (distanceCm < 0) return -1;
    if (distanceCm >= emptyCm) return 0;
    if (distanceCm <= fullCm) return 100;
    return (int)((long)(emptyCm - distanceCm) * 100 / (emptyCm - fullCm));
  }

  // Returns the level after this reading.
  Level update(int distanceCm) {
    int p = percentFor(distanceCm, emptyCm_, fullCm_);
    if (p < 0) return level_;  // no reading: keep the last level
    percent_ = p;
    const int up[] = {50, 80, 95};  // thresholds going up: medium, high, full
    Level target = LOW_LEVEL;
    for (uint8_t i = 0; i < 3; i++) {
      // to go up a level you need the threshold; to stay there, threshold - 5
      int needed = (level_ > i) ? up[i] - 5 : up[i];
      if (p >= needed) target = (Level)(i + 1);
    }
    level_ = target;
    return level_;
  }

  int percent() const { return percent_; }
  Level level() const { return level_; }
  bool full() const { return level_ == FULL_LEVEL; }

 private:
  int emptyCm_, fullCm_;
  int percent_ = 0;
  Level level_ = LOW_LEVEL;
};
