# Dog Tracker

A local browser app for checking a Heltec Wireless Tracker V2 over USB. It shows the latest position, recent trail and serial output. The demo uses a sample route. Position data stays in browser memory. The map loads tiles from OpenStreetMap, which receives tile requests for the area being viewed.

## Wi-Fi setup and automatic updates

`firmware/TrackerOTA` is the Wi-Fi and OTA firmware. It still streams GPS to the USB map. A one-time USB install is needed to switch from `TrackerBench` to `TrackerOTA` and install the 8 MB layout with two app slots. In Arduino IDE, use **ESP32S3 Dev Module**, **Flash Size → 8MB**, **Partition Scheme → 8M with spiffs (3MB APP/1.5MB SPIFFS)**, **USB Mode → Hardware CDC and JTAG**, and **USB CDC On Boot → Enabled**. The GitHub Actions USB image is a merged flash image for address `0x0`. The smaller OTA image is for the updater.

1. Connect USB and look for **Set up tracker Wi-Fi** on the map. It shows the tracker-specific network name and its randomly generated password. A separate serial monitor at 115200 baud can also read the `SETUP Wi-Fi` line. The setup network closes after five minutes. To reopen it, hold **USER** for three seconds while the tracker is running.
2. Join that temporary network from a phone or computer and open `http://192.168.4.1/`. Enter a **2.4 GHz** Wi-Fi network name and password. The tracker saves them and restarts.
3. Leave the GitHub token blank for public releases. If the [Dog-Tracker repository](https://github.com/snydergj/Dog-Tracker) is private, generate a [fine-grained GitHub token](https://github.com/settings/personal-access-tokens/new) restricted to that repository with **Contents: Read-only** and enter it directly into setup. The device stores its settings in local flash. Revoke the token if the tracker is lost, and reprovision it when the token expires.

GitHub Actions builds the firmware for changes to `main`, pull requests, and version tags. The first successful release build on `main` publishes `v1.0.0` once. For later device updates, increment `TRACKER_VERSION` in `firmware/TrackerOTA/Version.h`, merge the change, and push the matching tag (for example, `v1.0.1`). A matching tag publishes the binaries to GitHub Releases. On boot, the tracker checks for a newer release after Wi-Fi and clock sync. It downloads via HTTPS, compares the file's SHA-256 with GitHub's release metadata, and activates the other app slot after verifying the complete image. Keep the tracker powered through the update. Updates only need Wi-Fi at boot, and the GPS-to-map bench connection still uses USB.

## Start the app

From this folder, run `node server.mjs` and open [Dog Tracker](http://127.0.0.1:8787) in desktop Chrome or Edge. Keep the terminal running. The server uses Node.js and has no packages to install. Internet access is needed for Leaflet and the basemap. **Try demo** runs without a board.

## Test the factory firmware

1. With the board powered off, attach the matching LoRa antenna to its U.FL connector. Connect the tracker with a USB-C data cable. On this PC, Windows identified the tracker as **USB Serial Device (COM6)**. The port number can change after reconnecting or flashing.
2. Close any serial monitor. Click **Connect USB** in the app and choose the tracker. It listens at **115200 baud**.
3. If the screen shows the Heltec factory LoRa test, hold **USER** for about one second and release. Heltec's factory example enters GPS test after a long press. Look for `gps_test`, `gps_detected`, or `LAT:` and `LON:` in **Serial output**. Button behavior depends on the installed firmware. If double-pressing **USER** opens the Heltec F&T function menu, tap **USER** to highlight **GPS** and hold it to select the function.
4. Keep the board powered outdoors and point the screen toward open sky. Allow several minutes for the first fix. Keep USB connected to see the fix in the browser. If the board already has a fix on its own screen, reconnect USB to check whether the firmware streams it. The factory serial format may omit satellite counts and repeat its last coordinates after losing its fix. The optional NMEA sketch reports explicit fix validity and satellite counts when present.

If the port is missing, check Device Manager under **Ports (COM & LPT)** and try another data cable or USB port. A boot log or LoRa messages confirm that USB is working. If the display has coordinates and USB still only sends LoRa messages, use the GPS-only sketch below to stream positions. The map reads Heltec factory `LAT: ..., LON: ...` lines and standard checksummed GGA/RMC NMEA. Meshtastic and MeshCore use different serial protocols.

## Optional GPS-only firmware

Use `firmware/TrackerBench/TrackerBench.ino` if the shipped firmware cannot stream coordinates. Uploading replaces the running firmware. Save it first if you need to restore it. The sketch starts the onboard GNSS and sends NMEA over USB. It does not drive the display or transmit LoRa.

1. In Arduino IDE, install **esp32 by Espressif Systems** in Boards Manager. Select **ESP32S3 Dev Module** and the tracker's current USB port.
2. Set **Flash Size → 8MB**, **PSRAM → Disabled**, **USB CDC On Boot → Enabled**, **USB Mode → Hardware CDC and JTAG**, and **Upload Mode → UART0 / Hardware CDC** if shown.
3. Disconnect the browser from USB before uploading the sketch. If upload cannot connect, hold **USER/BOOT**, tap **RESET**, release **USER/BOOT**, then select the newly listed USB port and retry.
4. After upload, tap **RESET** if needed, then reopen the port in the app. Raw `$GNGGA` or `$GNRMC` sentences can arrive before a valid position.

The factory flash backup for this tracker is in `.tracker-backups/`. To restore it, close the app's USB connection and use Espressif esptool with `--chip esp32s3 --port COM6 write-flash 0x0 <backup.bin>`, substituting the current port and full backup path.

## Test a LoRa link next

For a standalone field test, use a second compatible LoRa node as the receiver. Match the radio band for both boards and use the region setting for your location. One route is the **Heltec Wireless Tracker V2** Meshtastic target on the tracker and a compatible Meshtastic node connected to your phone or PC. Test a position packet across the room, then move the tracker outdoors and increase distance gradually. This app currently uses USB, so bringing LoRa positions into it requires a receiver integration.

- [Heltec Wireless Tracker V2 docs](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wireless-tracker-v2/)
- [Heltec GNSS reception troubleshooting](https://heltec.org/project/wireless-tracker-v2/#faq)
- [Heltec V2 factory test source](https://github.com/HelTecAutomation/Heltec_ESP32/blob/master/examples/Factory_Test/Wireless_Tracker_V2_FactoryTest/Wireless_Tracker_V2_FactoryTest.ino)
- [Heltec F&T button and menu guide](https://wiki.heltec.org/docs/platform/f%26t/qs/)
- [Espressif USB CDC setup](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html)
- [Meshtastic Heltec hardware and flashing](https://meshtastic.org/docs/hardware/devices/heltec-automation/lora32/?heltec=tracker-v2)
