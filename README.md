# SIGNALDuino uC v4.0.0 with cc1101 support 
Master: ![unittest](https://github.com/RFD-FHEM/SIGNALDuino/workflows/unittest/badge.svg?branch=master)
### Getting started


System to receive digital signals and provide them to other systems for demodulation. Currently tested with 433 MHz, but not limited to that frequency or media.


Just clone the repo and open the project file with Visual Studio (only available for windows) or VSCode.
You can also open it with the Arduino IDE. 
Compile it and have fun.
If you are using the Arduino IDE, you have to copy all the libs into your sketch folder and modify some includes.

### Using SIGNALDuino in FHEM

If you want to use the SIGNALDuino with FHEM, you can use it directly from FHEM. No need to compile any sourcode.
You find more information here:
http://www.fhemwiki.de/wiki/SIGNALDuino



### Tested microcontrollers

* Arduino Nano
* Arduino Pro Mini
* ESP32 (ESP32-WROOM-32 / ESP32-WROOM-32D)
* ESP8266
* RadinoCC1101
* STM32 F103CBT6 (Maple Mini)

### ESP32 Notes

If you encounter problems compiling for ESP32, sorry the code for this microcontroller is currently not finished tested with all variants from ESP32. Contributors are welcome. If you have mane errors from fastDelegate.h try adding this compiler flag:
 -Wno-unused-local-typedef

### ESP web configuration

ESP32/ESP8266 builds serve a small configuration page on the device's IP (`/`, or `/sd` while the WiFi setup AP is active):
CC1101 module on/off, frequency, bWidth, rAmpl, sens, modulation, data rate, deviation, TX power, raw register writes,
decoder flags (MS, MU, MC, Mred, AFC, WMBus) for every CC1101 module, 10 banks to store/load complete CC1101 configurations, and firmware update (OTA) via `/update`.
The buttons "Standard 433 MHz" / "Standard 868 MHz" load the default register set (same as FHEM rfmode `SlowRF_ccFactoryReset`, ASK/OOK, bWidth 325 kHz, rAmpl 42 dB, sens 8 dB) with 433.920 MHz or 868.350 MHz and TX power 5 dBm.
"rfmode" loads one of the FSK presets of FHEM (Lacrosse, Bresser, Fine Offset, PCA301, KOPP_FC, WMBus_S/T, ...): factory settings plus the register values of the FHEM rfmode, WMBus is enabled for WMBus_S/T. The matching MN protocols must be enabled in FHEM.
To save resources the web server is stopped after 5 minutes uptime unless the configuration page was opened;
once opened it stays active until "Speichern & Neustart".

System settings in the web configuration: module names, frequency offset per module (crystal correction via FSCTRL0, 1.59 kHz steps,
also used as start value for AFC), MC min bit length per module, LED pin (empty = no LED) and inverse, hostname, web server timeout
(minutes, 0 = always on). ESP32 only: user / password (HTTP basic auth for all pages incl. OTA, not in the WiFi setup portal;
forgotten password: flash with "Erase Flash"), backup (`/bk`, file `.sdc`) and restore of the complete configuration.
The status page (`/custom`) shows per module frequency, mode, received messages, current RSSI and whether FHEM is connected.

### ESP32 with up to 4 CC1101

The ESP32 CC1101 build (`esp32cc1101`) supports up to 4 CC1101 modules on the same SPI bus. Every module gets its own telnet port,
so FHEM uses every module as its own SIGNALduino device (e.g. `192.168.0.10:24`), no changes in FHEM are needed.
Modules 2-4 are only used if a CC1101 is detected at boot. Pinout (compatible to the SIGNALESP32 board, module 1 = former single CC1101):

| Module | Port | CS | GDO0 (TX) | GDO2 (RX) |
| :--- | :--- | :--- | :--- | :--- |
| 1 | 23 | 5 | 4 | 13 |
| 2 | 24 | 27 | 26 | 25 |
| 3 | 25 | 22 | - | 14 |
| 4 | 26 | 33 | - | 21 |

SCK 18, MISO 19, MOSI 23 are shared. Modules without GDO0 receive ASK/OOK and xFSK, but can only send xFSK and cannot use WMBus.
WMBus can be used on one module at a time. All pins (SCK, MISO, MOSI, CS/GDO0/GDO2 per module) can be changed in the web configuration (active after reboot, module without CS = not used). Default pins can be changed with `RADIO_CS_PINS`, `RADIO_SEND_PINS`, `RADIO_RECEIVE_PINS` and the number of modules with `NUM_RADIO` (build flags, see `src/radio.h`).

### ESP32 builds (GitHub Actions)

`.github/workflows/esp32.yml` builds `esp32cc1101`, `esp32s`, `esp32cc1101_core2`, `esp8266cc1101` and `esp8266s` on every push and pull request.
The artifacts contain `SIGNALDuino_<env>*.bin` for the OTA update (`/update`, ESP8266 also USB flash at 0x0) and for ESP32 `SIGNALDuino_<env>*_factory.bin` to flash via USB at address 0x0.
On push and manual start all files are also stored as draft release (Releases page, one draft per branch, the previous draft of the branch is replaced).
Publishing the draft creates the tag `esp32-<branch>-<commit>`. Requires "Read and write permissions" for workflows (Settings > Actions > General) if the repository restricts the token.

#### Arduino core 3.x (pioarduino)

`esp32s` and `esp32cc1101` use Arduino core 3.x (ESP-IDF 5) from [pioarduino](https://github.com/pioarduino/platform-espressif32), the official PlatformIO platform only provides core 2.x.
`esp32cc1101_core2` (core 2.0.14, ESP-IDF 4.4) is kept as fallback. The core 3 builds use the partition table `min_spiffs.csv` (2 x 1.9 MB app instead of 2 x 1.25 MB).

* **Changing from a core 2 firmware to this version requires flashing the `_factory.bin` via USB** (new partition scheme), an OTA update is not sufficient.
  This erases the configuration (EEPROM in NVS) and WiFi settings, so make a backup (`/bk`) first and restore it afterwards. Later updates via OTA (`/update`).
* Local build: PlatformIO Core 6.1 or newer, in VS Code the PlatformIO or pioarduino IDE extension.

#### Flash the factory image with a web installer

The `_factory.bin` contains bootloader, partition table and firmware and is flashed at address **0x0**. No software installation is needed,
a browser with Web Serial support (Chrome, Edge, Opera on desktop) is enough:

* **Espressif esptool-js** (official): https://espressif.github.io/esptool-js/
  1. Connect the ESP32 via USB.
  2. Press the **BOOT** button and keep it pressed until the serial port has been selected in the browser.
  3. Click *Connect*, select the serial port, release BOOT after the connection is established.
  4. Set *Flash Address* to `0x0`, choose the `_factory.bin`, click *Program*.

  Optional *Erase Flash* before programming deletes the stored WiFi and CC1101 configuration.
* **ESPHome Web**: https://web.esphome.io/ , hold **BOOT** as above, *Connect*, select the port, *Install*, select the `_factory.bin` (written at 0x0).

If the port does not appear, install the USB driver (CP210x or CH340). If the connection fails with "Invalid head of packet" or `downloadMode:false`,
the ESP32 is not in download mode: hold BOOT, press EN/RST briefly, release BOOT and connect again.
After flashing, connect to the open WiFi access point of the SIGNALDuino to configure WiFi (or restore the backup). Later updates use the OTA `.bin` via `/update`.

### Signal from my device ist not detected

Implemented is a pattern detection engine, that can detect serval signal types. May not all, but most of them.

Uncomment #define debugdetect in libs/remotesensor/patterdecoder.h
Search for some output which describes a pattern with serval bits received.
If you find something, open an issue and provide as much as possible informations with it.


### You found a bug

First, sorry. This software is not perfect.
1. Open a issue
-With helpful title - use descriptive keywords in the title and body so others can find your bug (avoiding duplicates).
- Which branch, what microcontroller, what setup
- Steps to reproduce the problem, with actual vs. expected results
- If you find a bug in our code, post the files and the lines. 

### Contributing

1. Open one ore more issue for your development.
2. Ask to be added to our repository or just fork it.
3. Make your modifications and test them.
4. Create a branch (git checkout -b my_branch)
5. Commit your changes (git commit -am "<some description>")
6 .Push to a developer branch (git push dev-<xyz >my_branch)
7. Open a Pull Request, put some useful informations there, what your extension does and why we should add it, reference to the open issues which are fixed whith this pull requet.
