#pragma once

enum class DisplayNetwork { Offline, Joining, Connected, Setup };

struct Dashboard {
  int utcSeconds;
  int satellites;
  bool gpsFix;
  float chipCelsius;
  DisplayNetwork network;
};

void startTrackerDisplay();
void drawTrackerDisplay(const Dashboard &dashboard);
