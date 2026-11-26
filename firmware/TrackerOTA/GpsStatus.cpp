#include "GpsStatus.h"

#include <string.h>

namespace {
bool digit(char character) {
  return character >= '0' && character <= '9';
}

int hexDigit(char character) {
  if (digit(character)) return character - '0';
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  return -1;
}

int smallNumber(const char *value) {
  int number = 0;
  if (!*value) return -1;
  for (; *value; ++value) {
    if (!digit(*value) || number > 9) return -1;
    number = number * 10 + *value - '0';
  }
  return number;
}

int parseTime(const char *value) {
  for (int i = 0; i < 6; ++i) if (!digit(value[i])) return -1;
  if (value[6] && value[6] != '.') return -1;
  const int hour = (value[0] - '0') * 10 + value[1] - '0';
  const int minute = (value[2] - '0') * 10 + value[3] - '0';
  const int second = (value[4] - '0') * 10 + value[5] - '0';
  return hour < 24 && minute < 60 && second < 60 ? hour * 3600 + minute * 60 + second : -1;
}
}

void GpsStatus::consume(char character, uint32_t now) {
  if (character == '$') {
    sentence_[0] = character;
    length_ = 1;
  } else if (length_) {
    if (character == '\r' || character == '\n') {
      sentence_[length_] = '\0';
      parse(now);
      length_ = 0;
    } else if (length_ < sizeof(sentence_) - 1 && character >= ' ' && character <= '~') {
      sentence_[length_++] = character;
    } else {
      length_ = 0;
    }
  }
}

void GpsStatus::parse(uint32_t now) {
  if (length_ < 10 || strncmp(sentence_ + 3, "GGA,", 4) != 0) return;
  char *checksum = strchr(sentence_ + 7, '*');
  if (!checksum || strlen(checksum) != 3) return;
  const int high = hexDigit(checksum[1]);
  const int low = hexDigit(checksum[2]);
  if (high < 0 || low < 0) return;
  unsigned parity = 0;
  for (const char *letter = sentence_ + 1; letter < checksum; ++letter) parity ^= *letter;
  if (parity != static_cast<unsigned>(high * 16 + low)) return;
  *checksum = '\0';

  char *fields[7];
  fields[0] = sentence_ + 7;
  for (unsigned index = 1; index < 7; ++index) {
    char *comma = strchr(fields[index - 1], ',');
    if (!comma) return;
    *comma = '\0';
    fields[index] = comma + 1;
  }
  if (char *comma = strchr(fields[6], ',')) *comma = '\0';
  const int quality = smallNumber(fields[5]);
  const int count = smallNumber(fields[6]);
  if (quality < 0 || count < 0) return;
  fix_ = quality > 0;
  satellites_ = count;
  fixReceivedAt_ = now;
  const int gpsTime = parseTime(fields[0]);
  if (fix_ && gpsTime >= 0) {
    clockSeconds_ = gpsTime;
    clockReceivedAt_ = now;
  }
}

bool GpsStatus::current(uint32_t now) const {
  return satellites_ >= 0 && now - fixReceivedAt_ < 5000;
}

int GpsStatus::satellites(uint32_t now) const {
  return current(now) ? satellites_ : -1;
}

bool GpsStatus::hasFix(uint32_t now) const {
  return current(now) && fix_;
}

int GpsStatus::secondsUtc(uint32_t now) const {
  const uint32_t elapsed = now - clockReceivedAt_;
  if (clockSeconds_ < 0 || elapsed >= 86400000) return -1;
  return (clockSeconds_ + elapsed / 1000) % 86400;
}
