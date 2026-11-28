#include "GpsStatus.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {
void receive(GpsStatus &gps, const std::string &payload, uint32_t now, bool corrupt = false) {
  unsigned checksum = 0;
  for (const char character : payload) checksum ^= character;
  char suffix[6];
  std::snprintf(suffix, sizeof(suffix), "*%02X\r\n", checksum ^ (corrupt ? 1 : 0));
  for (const char character : "$" + payload + suffix) gps.consume(character, now);
}
}

int main() {
  GpsStatus gps;
  assert(gps.satellites(0) == -1 && !gps.hasFix(0) && gps.secondsUtc(0) == -1);
  assert(!*gps.latestGga(0));
  receive(gps, "GNGGA,235958.00,4100.000,N,08700.000,W,1,14,0.8,0,M,0,M,,", 500);
  assert(gps.satellites(500) == 14 && gps.hasFix(500));
  const std::string validGga = gps.latestGga(500);
  assert(validGga.find("$GNGGA,235958.00,4100.000,N,08700.000,W,1,14,") == 0);
  assert(validGga[validGga.size() - 3] == '*');
  assert(gps.secondsUtc(500) == 86398 && gps.secondsUtc(2500) == 0);
  receive(gps, "GPGGA,000001.00,,,,,0,00,9.9,,,,,,", 3500, true);
  assert(gps.satellites(3500) == 14 && gps.hasFix(3500));
  assert(validGga == gps.latestGga(3500));
  receive(gps, "GPGGA,000001.00,,,,,0,00,9.9,,,,,,", 3500);
  assert(gps.satellites(3500) == 0 && !gps.hasFix(3500));
  assert(std::string(gps.latestGga(3500)).find("$GPGGA,000001.00,,,,,0,00,") == 0);
  assert(gps.satellites(8500) == -1 && gps.secondsUtc(8500) == 6);
  assert(!*gps.latestGga(8500));

  for (const char character : "$" + std::string(150, 'X')) gps.consume(character, 9000);
  receive(gps, "GAGGA,080023,4100.000,N,08700.000,W,2,09,0.8,0,M,0,M,,", 9000);
  assert(gps.hasFix(9000) && gps.satellites(9000) == 9 && gps.secondsUtc(9000) == 28823);
}
