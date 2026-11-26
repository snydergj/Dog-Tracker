#include "TrackerDisplay.h"

#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>

#include "Version.h"

namespace {
constexpr int kScreenWidth = 160;
constexpr int kScreenHeight = 80;
constexpr int kBacklight = 21;
constexpr int kChipSelect = 38;
constexpr int kDataCommand = 40;
constexpr int kReset = 39;
constexpr int kClock = 41;
constexpr int kMosi = 42;

constexpr uint16_t rgb(unsigned red, unsigned green, unsigned blue) {
  return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

constexpr uint16_t kBackground = rgb(10, 19, 32);
constexpr uint16_t kSurface = rgb(25, 41, 57);
constexpr uint16_t kText = rgb(233, 246, 249);
constexpr uint16_t kMuted = rgb(157, 181, 199);
constexpr uint16_t kMint = rgb(102, 240, 184);
constexpr uint16_t kAmber = rgb(255, 194, 109);

SPIClass displayBus(HSPI);
Adafruit_ST7735 screen(&displayBus, kChipSelect, kDataCommand, kReset);
GFXcanvas16 frame(kScreenWidth, kScreenHeight);

void label(int x, int y, const char *text, uint16_t color, int size = 1) {
  frame.setTextSize(size);
  frame.setTextColor(color);
  frame.setCursor(x, y);
  frame.print(text);
}

void statusPill(const char *status, uint16_t color) {
  const int width = strlen(status) * 6 + 16;
  const int x = kScreenWidth - width - 6;
  frame.fillRoundRect(x, 4, width, 13, 5, kSurface);
  frame.fillCircle(x + 6, 10, 2, color);
  label(x + 11, 7, status, color);
}

const char *networkLabel(DisplayNetwork status) {
  switch (status) {
    case DisplayNetwork::Connected: return "WI-FI ONLINE";
    case DisplayNetwork::Joining: return "WI-FI JOINING";
    case DisplayNetwork::Setup: return "WI-FI SETUP";
    default: return "WI-FI OFFLINE";
  }
}
}

void startTrackerDisplay() {
  pinMode(kBacklight, OUTPUT);
  digitalWrite(kBacklight, LOW);
  if (!frame.getBuffer()) return;
  displayBus.begin(kClock, -1, kMosi, kChipSelect);
  screen.initR(INITR_MINI160x80);
  screen.setSPISpeed(24000000);
  screen.setRotation(1);
  // The MINI profile supplies the V2 offsets. Heltec's panel uses BGR order.
  const uint8_t landscapeBgr = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | ST7735_MADCTL_BGR;
  screen.sendCommand(ST77XX_MADCTL, &landscapeBgr, 1);
  screen.invertDisplay(false);
  frame.setTextWrap(false);
  screen.fillScreen(kBackground);
  digitalWrite(kBacklight, HIGH);
}

void drawTrackerDisplay(const Dashboard &dashboard) {
  if (!frame.getBuffer()) return;
  frame.fillScreen(kBackground);
  label(7, 7, "DOG TRACKER", kText);
  statusPill(dashboard.gpsFix ? "GPS FIX" : dashboard.satellites >= 0 ? "SEARCH" : "NO GPS",
             dashboard.gpsFix ? kMint : kAmber);
  frame.drawFastHLine(6, 19, 148, kSurface);

  char clock[8] = "--:--";
  if (dashboard.utcSeconds >= 0) {
    snprintf(clock, sizeof(clock), "%02d:%02d", dashboard.utcSeconds / 3600,
             dashboard.utcSeconds / 60 % 60);
  }
  label(6, 35, clock, kText, 3);
  label(8, 25, "UTC TIME", kMuted);

  frame.fillRoundRect(103, 22, 51, 45, 4, kSurface);
  label(108, 26, "SATS", kMuted);
  label(108, 49, "CHIP C", kMuted);
  char value[8] = "--";
  if (dashboard.satellites >= 0) snprintf(value, sizeof(value), "%02d", dashboard.satellites);
  label(108, 35, value, dashboard.gpsFix ? kMint : kAmber, 2);
  strcpy(value, "--");
  if (isfinite(dashboard.chipCelsius) && dashboard.chipCelsius >= -40 && dashboard.chipCelsius <= 125) {
    snprintf(value, sizeof(value), "%ld", lroundf(dashboard.chipCelsius));
  }
  label(108, 58, value, kText);

  frame.drawFastHLine(6, 69, 148, kSurface);
  frame.fillCircle(9, 75, 2, dashboard.network == DisplayNetwork::Connected ? kMint : kMuted);
  label(15, 72, networkLabel(dashboard.network), kMuted);
  const char version[] = "v" TRACKER_VERSION;
  label(154 - (sizeof(version) - 1) * 6, 72, version, kMuted);

  screen.startWrite();
  screen.setAddrWindow(0, 0, kScreenWidth, kScreenHeight);
  screen.writePixels(frame.getBuffer(), kScreenWidth * kScreenHeight);
  screen.endWrite();
}
