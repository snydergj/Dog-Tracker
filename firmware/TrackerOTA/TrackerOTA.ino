#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "GpsStatus.h"
#include "TrackerDisplay.h"
#include "Updater.h"
#include "Version.h"

namespace {
constexpr int kGnssPower = 3;
constexpr int kGnssRx = 33;
constexpr int kGnssTx = 34;
constexpr int kUserButton = 0;
constexpr unsigned long kBaud = 115200;
constexpr unsigned long kJoinTimeout = 25000;
constexpr unsigned long kPortalTimeout = 300000;

struct Settings {
  char network[33];
  char password[65];
  char token[256];
};

Settings settings{};
Preferences storage;
WebServer portal(80);
GpsStatus gpsStatus;
bool connecting = false;
bool portalOpen = false;
bool checkedForUpdate = false;
bool buttonHandled = false;
unsigned long networkStarted;
unsigned long lastPortalNotice;
unsigned long buttonDown;
unsigned long restartAt;
char setupNetwork[24];
char setupPassword[17];
char formNonce[17];

void printPortalNotice() {
  Serial.printf("SETUP Wi-Fi: %s | Password: %s | http://192.168.4.1/\n", setupNetwork, setupPassword);
  lastPortalNotice = millis();
}

void showForm() {
  String page = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Dog Tracker Wi-Fi</title>
<style>body{font:1rem/1.5 system-ui;margin:3rem auto;padding:0 1rem;max-width:28rem;color:#162d26}label{display:block;margin-top:1rem}input,button{display:block;font:inherit;padding:.55rem;box-sizing:border-box;width:100%;margin-top:.25rem}button{background:#0d5c43;color:white;border:0;border-radius:.4rem;margin-top:1.25rem}p{color:#435c53}</style>
<h1>Dog Tracker Wi-Fi</h1><form method="post" action="/setup">
<label>2.4 GHz network name<input name="network" required maxlength="32" autocomplete="off"></label>
<label>Wi-Fi password<input name="password" type="password" maxlength="64" autocomplete="new-password"></label>
<label>GitHub update token (optional)<input name="token" type="password" maxlength="255" autocomplete="new-password"></label>
<p>For a private repository, use a fine-grained token with read-only Contents access to Dog-Tracker. Public releases can be checked without a token.</p><input type="hidden" name="nonce" value=")HTML";
  page += formNonce;
  page += "\"><button>Save and restart</button></form></html>";
  portal.sendHeader("Cache-Control", "no-store");
  portal.send(200, "text/html; charset=utf-8", page);
}

void saveForm() {
  if (restartAt || portal.arg("nonce") != formNonce) {
    portal.send(403, "text/plain", "Reopen the setup page and try again.");
    return;
  }
  const String network = portal.arg("network");
  const String password = portal.arg("password");
  const String token = portal.arg("token");
  bool tokenValid = !token.length() || (token.startsWith("github_pat_") && token.length() > 11);
  for (size_t i = 0; tokenValid && i < token.length(); ++i) {
    const char character = token[i];
    tokenValid = isalnum(static_cast<unsigned char>(character)) || character == '_';
  }
  if (!network.length() || network.length() >= sizeof(settings.network) ||
      password.length() >= sizeof(settings.password) || (password.length() && password.length() < 8) ||
      token.length() >= sizeof(settings.token) || !tokenValid) {
    portal.send(400, "text/plain", "Check the network name, Wi-Fi password and fine-grained token.");
    return;
  }
  Settings next{};
  network.toCharArray(next.network, sizeof(next.network));
  password.toCharArray(next.password, sizeof(next.password));
  token.toCharArray(next.token, sizeof(next.token));
  if (storage.putBytes("settings", &next, sizeof(next)) != sizeof(next)) {
    portal.send(500, "text/plain", "Could not save the settings. Try again.");
    return;
  }
  portal.sendHeader("Cache-Control", "no-store");
  portal.send(200, "text/plain", "Settings saved. The tracker will restart and join the network.");
  restartAt = millis() + 1500;
}

void startPortal() {
  if (portalOpen) return;
  connecting = false;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  snprintf(setupNetwork, sizeof(setupNetwork), "DogTracker-%06X", static_cast<unsigned>(ESP.getEfuseMac() & 0xffffff));
  snprintf(setupPassword, sizeof(setupPassword), "%08X%08X", esp_random(), esp_random());
  snprintf(formNonce, sizeof(formNonce), "%08X%08X", esp_random(), esp_random());
  if (!WiFi.softAP(setupNetwork, setupPassword, 1, 0, 1)) {
    Serial.println("SETUP could not start. Hold USER to retry.");
    WiFi.mode(WIFI_OFF);
    return;
  }
  portal.begin();
  portalOpen = true;
  networkStarted = millis();
  printPortalNotice();
}

void updateTask(void *) {
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  const unsigned long start = millis();
  while (time(nullptr) < 1700000000 && millis() - start < 20000) vTaskDelay(pdMS_TO_TICKS(250));
  if (time(nullptr) >= 1700000000) checkForTrackerUpdate(settings.token[0] ? settings.token : nullptr);
  else Serial.println("OTA skipped because the clock could not synchronize.");
  vTaskDelete(nullptr);
}

void serviceNetwork() {
  const unsigned long now = millis();
  if (portalOpen) {
    portal.handleClient();
    if (restartAt && static_cast<long>(now - restartAt) >= 0) ESP.restart();
    if (now - lastPortalNotice >= 10000) printPortalNotice();
    if (!restartAt && now - networkStarted >= kPortalTimeout) {
      portal.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      portalOpen = false;
      Serial.println("SETUP closed. Hold USER for three seconds to reopen it.");
    }
  } else if (connecting && WiFi.status() == WL_CONNECTED) {
    connecting = false;
    Serial.println("Wi-Fi connected.");
    if (!checkedForUpdate) {
      checkedForUpdate = true;
      if (xTaskCreate(updateTask, "OTA", 12288, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("OTA could not start.");
      }
    }
  } else if (connecting && now - networkStarted >= kJoinTimeout) {
    startPortal();
  }

  if (digitalRead(kUserButton) == HIGH) {
    buttonDown = now;
    buttonHandled = false;
  } else if (!buttonHandled && now - buttonDown >= 3000) {
    // Restart into setup. An incomplete download never becomes the active image.
    buttonHandled = true;
    if (!portalOpen && storage.putBool("setup", true)) ESP.restart();
  }
}

void refreshDashboard(uint32_t now) {
  static uint32_t drawnAt = 0;
  if (now - drawnAt < 1000) return;
  drawnAt = now;

  const time_t systemTime = time(nullptr);
  const int utc = systemTime >= 1700000000 ? static_cast<int>(systemTime % 86400) : gpsStatus.secondsUtc(now);
  DisplayNetwork network = DisplayNetwork::Offline;
  if (portalOpen) network = DisplayNetwork::Setup;
  else if (connecting) network = DisplayNetwork::Joining;
  else if (WiFi.status() == WL_CONNECTED) network = DisplayNetwork::Connected;
  drawTrackerDisplay({utc, gpsStatus.satellites(now), gpsStatus.hasFix(now), temperatureRead(), network});
}
}

void setup() {
  Serial.begin(kBaud);
  Serial.printf("Dog Tracker firmware %s\n", TRACKER_VERSION);
  pinMode(kGnssPower, OUTPUT);
  digitalWrite(kGnssPower, HIGH);
  Serial1.setRxBufferSize(2048);
  Serial1.begin(kBaud, SERIAL_8N1, kGnssRx, kGnssTx);
  startTrackerDisplay();
  pinMode(kUserButton, INPUT_PULLUP);
  buttonDown = millis();

  storage.begin("dog-tracker", false);
  if (storage.getBytesLength("settings") == sizeof(settings)) storage.getBytes("settings", &settings, sizeof(settings));
  const bool forceSetup = storage.getBool("setup", false);
  if (forceSetup) storage.remove("setup");
  portal.on("/", HTTP_GET, showForm);
  portal.on("/setup", HTTP_POST, saveForm);
  WiFi.persistent(false);
  if (forceSetup || !settings.network[0]) {
    startPortal();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.network, settings.password);
    networkStarted = millis();
    connecting = true;
  }
}

void loop() {
  const uint32_t now = millis();
  for (int remaining = 256; remaining > 0 && Serial1.available(); --remaining) {
    const uint8_t character = Serial1.read();
    Serial.write(character);
    gpsStatus.consume(static_cast<char>(character), now);
  }
  serviceNetwork();
  refreshDashboard(now);
  delay(1);
}
