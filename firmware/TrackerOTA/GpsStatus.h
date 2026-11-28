#pragma once

#include <stdint.h>

class GpsStatus {
 public:
  void consume(char character, uint32_t now);
  int satellites(uint32_t now) const;
  bool hasFix(uint32_t now) const;
  int secondsUtc(uint32_t now) const;
  const char *latestGga(uint32_t now) const;

 private:
  void parse(uint32_t now);
  bool current(uint32_t now) const;

  char sentence_[128]{};
  char latestGga_[128]{};
  unsigned length_ = 0;
  int satellites_ = -1;
  bool fix_ = false;
  uint32_t fixReceivedAt_ = 0;
  int clockSeconds_ = -1;
  uint32_t clockReceivedAt_ = 0;
};
