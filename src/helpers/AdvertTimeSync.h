#pragma once

// Sets the node clock from the timestamps of received adverts, for nodes that
// have no RTC and no app connected to set the time.
//
// Only works while the clock is NOT set (earlier than MIN_VALID_TIME), and
// only trusts the network when several different nodes agree:
//  - every advert gives one sample: advert timestamp - our uptime (an offset),
//    one sample per node (the latest advert of that node wins)
//  - adverts with an implausible timestamp (unset clocks of other nodes) are ignored
//  - once MIN_AGREE nodes lie within AGREE_WINDOW_S of the median offset, the
//    clock is set to the median of those and syncing stops
// A single node with a wrong clock, or a replayed old advert, cannot move the time.

#include <Arduino.h>
#include <MeshCore.h>

class AdvertTimeSync {
public:
  // clocks before this are "not set" (the ESP32 fallback clock starts in May 2024
  // after power-on, RTC chips without a battery start in 2000)
  static const uint32_t MIN_VALID_TIME = 1767225600UL;   // 2026-01-01 00:00 UTC
  static const uint32_t MAX_VALID_TIME = MIN_VALID_TIME + 30UL * 365 * 24 * 3600;

  static bool isTimeValid(uint32_t t) { return t >= MIN_VALID_TIME && t < MAX_VALID_TIME; }

  #define ADVERT_SYNC_SLOTS    8
  #define ADVERT_SYNC_KEY_LEN  6
  static const int MIN_AGREE = 3;
  static const int32_t AGREE_WINDOW_S = 600;    // flood delay + clock drift between nodes

  AdvertTimeSync() : _count(0) { }

  // feed every verified advert; returns true when it has just set the clock
  bool onAdvert(mesh::RTCClock& clock, const uint8_t* pub_key, uint32_t advert_time) {
    if (isTimeValid(clock.getCurrentTime())) {   // already set (app, RTC chip or earlier sync)
      _count = 0;
      return false;
    }
    if (!isTimeValid(advert_time)) return false;

    int64_t offset = (int64_t)advert_time - (int64_t)(millis() / 1000);
    addSample(pub_key, offset);

    int64_t t;
    if (!agreedOffset(t)) return false;
    clock.setCurrentTime((uint32_t)(t + (int64_t)(millis() / 1000)));
    _count = 0;
    return true;
  }

private:
  struct Sample {
    uint8_t key[ADVERT_SYNC_KEY_LEN];
    int64_t offset;
  };
  Sample _samples[ADVERT_SYNC_SLOTS];
  int _count;

  void addSample(const uint8_t* pub_key, int64_t offset) {
    for (int i = 0; i < _count; i++) {
      if (memcmp(_samples[i].key, pub_key, ADVERT_SYNC_KEY_LEN) == 0) {
        _samples[i].offset = offset;           // same node again: keep its latest advert
        return;
      }
    }
    if (_count == ADVERT_SYNC_SLOTS) {         // full: drop the oldest node
      memmove(&_samples[0], &_samples[1], (ADVERT_SYNC_SLOTS - 1) * sizeof(Sample));
      _count--;
    }
    memcpy(_samples[_count].key, pub_key, ADVERT_SYNC_KEY_LEN);
    _samples[_count].offset = offset;
    _count++;
  }

  static void sortOffsets(int64_t* v, int n) {
    for (int i = 1; i < n; i++) {               // insertion sort, n <= 8
      int64_t x = v[i];
      int j = i - 1;
      while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
      v[j + 1] = x;
    }
  }

  bool agreedOffset(int64_t& result) const {
    if (_count < MIN_AGREE) return false;
    int64_t v[ADVERT_SYNC_SLOTS];
    for (int i = 0; i < _count; i++) v[i] = _samples[i].offset;
    sortOffsets(v, _count);
    int64_t median = v[_count / 2];

    // the samples that agree with the median
    int64_t agree[ADVERT_SYNC_SLOTS];
    int n = 0;
    for (int i = 0; i < _count; i++) {
      int64_t d = v[i] - median;
      if (d >= -AGREE_WINDOW_S && d <= AGREE_WINDOW_S) agree[n++] = v[i];
    }
    if (n < MIN_AGREE) return false;
    result = agree[n / 2];                      // already sorted
    return true;
  }
};
