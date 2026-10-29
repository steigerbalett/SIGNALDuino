#pragma once

#if defined (ESP32) || defined(ESP8266)
#include "compile_config.h"

#define PROGNAME               " SIGNALESP "
#define VERSION_1              0x33
#define VERSION_2              0x1d
#define BAUDRATE               115200
#define FIFO_LENGTH            255

#define ETHERNET_PRINT
//#define WIFI_MANAGER_OVERRIDE_STRINGS

// EEPROM addresses
#define EE_MAGIC_OFFSET        0
#define addr_features          0xff
#define EE_IP                  0x100 // 4 byte for IP address
#define EE_GW                  0x104 // 4 byte for gateway address
#define EE_SNM                 0x108 // 4 byte for subnet mask
#define EE_WM_THEME            0x110 // 1 byte for theme dark/light
#define EE_CC_OFF              0x111 // 1 byte, 1 = cc1101 module disabled (web config)
#define EE_BANK_ACT            0x112 // 1 byte, active bank, 0xFF = none
#define EE_BANK_BASE           0x120 // banks for cc1101 configuration (web config)
#define EE_BANK_SIZE           0x40
#define EE_BANK_COUNT          10
#define EE_BANK_MARK           0xB5
#define EE_BANK_NAMELEN        12
#define EE_SIZE                0x480 // banks end at 0x3A0, then radio 2-4 and pins (radio.h)
#define MAX_SRV_CLIENTS        2

void serialEvent();
int freeRam();
inline void ethernetEvent();
size_t writeCallback(const uint8_t *buf, uint8_t len = 1);

#if defined(ESP8266)
  extern "C" {
    #include "user_interface.h"
  }

  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>   // Local WebServer used to serve the configuration portal
#endif

#if defined(ESP32)
  #include "esp_timer.h"
  #include "esp_task_wdt.h"
  #include <WiFi.h>
  #include <WiFiType.h>
#endif

#include <FS.h>
#include <EEPROM.h>
//#include <DNSServer.h>             // Local DNS Server used for redirecting all requests to the configuration portal

#include "output.h"
#include "bitstore.h"              // Die wird aus irgend einem Grund zum Compilieren benoetigt.
#include "SimpleFIFO.h"

#ifdef CMP_CC1101
  #include "cc1101.h"
  #include <SPI.h>                 // prevent travis errors
  #include "mbus.h"
#endif

uint8_t curRadio = 0;              // current radio, see radio.h
bool radioUsed[NUM_RADIO];         // radio found (radio 1 always used, also without cc1101)
uint8_t radioCsPin[NUM_RADIO] = RADIO_CS_PINS;
uint8_t radioPinSend[NUM_RADIO] = RADIO_SEND_PINS;
uint8_t radioPinReceive[NUM_RADIO] = RADIO_RECEIVE_PINS;
uint8_t radioSpiPin[3] = RADIO_SPI_PINS;

void radioLoadPins() {             // pins from web configuration
#ifdef RADIO_PIN_CFG
  if (EEPROM.read(EE_PIN_BASE) != EE_PIN_MARK) return;
  for (uint8_t i = 0; i < 3; i++) radioSpiPin[i] = EEPROM.read(EE_PIN_BASE + 1 + i);
  for (uint8_t r = 0; r < NUM_RADIO; r++) {
    radioCsPin[r] = EEPROM.read(EE_PIN_BASE + 4 + r * 3);
    radioPinSend[r] = EEPROM.read(EE_PIN_BASE + 5 + r * 3);
    radioPinReceive[r] = EEPROM.read(EE_PIN_BASE + 6 + r * 3);
  }
#endif
}
SimpleFIFO<int, FIFO_LENGTH> FiFos[NUM_RADIO]; //store FIFO_LENGTH # ints per radio
#include "signalDecoder.h"
#include "commands.h"
#include "functions.h"
#include "send.h"

//#define WIFI_MANAGER_OVERRIDE_STRINGS
#include "WiFiManager.h"           // https://github.com/tzapu/WiFiManager
WiFiManager wifiManager;
WiFiManagerParameter custom_field; // global param ( for non blocking w params )
// access point for the first WiFi setup without password (open AP)
// 2025-02-13 - Arduino Release v3.1.2 based on ESP-IDF v5.3 (seit dem AP nicht mehr ohne Passwort, ohne PW Neustart)
// -> with ESP-IDF 5.x the password stays, can be overwritten with build flag -D WM_AP_PASSWORD=\"\"
#ifndef WM_AP_PASSWORD
  #if defined(ESP32) && defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    #define WM_AP_PASSWORD "signalesp"
  #else
    #define WM_AP_PASSWORD ""            // open access point
  #endif
#endif
#include "web-config.h"
#include "wifi-config.h"

WiFiServer *Servers[NUM_RADIO];    //  telnet, port 23 + radio
WiFiClient serverClients[NUM_RADIO];

SignalDetectorClass musterDecs[NUM_RADIO];

#define pulseMin  90
volatile bool blinkLED = false;
String cmdstring = "";
volatile unsigned long lastTimes[NUM_RADIO];

/*
#define digitalLow(P) digitalWrite(P,LOW)
#define digitalHigh(P) digitalWrite(P,HIGH)
#define isHigh(P) (digitalRead(P) == HIGH)
#define isLow(P) (digitalRead(P) == LOW)
#define digitalState(P)((uint8_t)isHigh(P))
*/

#ifdef ESP32
  esp_timer_create_args_t cronTimer_args;
  esp_timer_create_args_t blinksos_args;
  esp_timer_handle_t cronTimer_handle;
  esp_timer_handle_t blinksos_handle;
#elif defined(ESP8266)
  os_timer_t cronTimer;
  os_timer_t blinksos;
#endif

bool hasCC1101s[NUM_RADIO];
bool ccDisableds[NUM_RADIO];
bool AfcEnableds[NUM_RADIO];
char IB_1s[NUM_RADIO][14];         // Input Buffer one - capture commands
bool wmbuses[NUM_RADIO];
bool wmbus_ts[NUM_RADIO];
const char sos_sequence[] = "0101010001110001110001110001010100000000";
const char boot_sequence[] = "00010100111";


void IRAM_ATTR sosBlink (void *pArg) {
  static uint8_t pos = 0;
  const char* pChar;
  pChar = (const char*)pArg;      //OK in both C and C++

  digitalWrite(PIN_LED, pChar[pos] == '1' ? HIGH : LOW);
  pos++;
  if (pos == sizeof(pChar) * sizeof(pChar[1]))
    pos = 0;
}

#ifdef ESP8266
  WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
#endif

void restart(){
  ESP.restart();
}

void IRAM_ATTR cronjob(__attribute__((unused)) void *pArg) {
  cli();
  static uint16_t cnt = 0; // approximately every 35 minutes

  for (uint8_t r = 0; r < NUM_RADIO; r++) {  // curRadio is not valid here (timer task)
    if (!radioUsed[r]) continue;
    const unsigned long  duration = micros() - lastTimes[r];

    if (duration > maxPulse) {       // Auf Maximalwert pruefen.
      int sDuration = maxPulse;
      if (isLow(radioPinReceive[r])) {      // Wenn jetzt low ist, ist auch weiterhin low
        sDuration = -sDuration;
      }
      FiFos[r].enqueue(sDuration);
      lastTimes[r] = micros();
    }
  }
#ifdef PIN_LED_INVERSE
  digitalWrite(PIN_LED, !blinkLED);
#else
  digitalWrite(PIN_LED, blinkLED);
#endif
  blinkLED = false;

  // Infrequent time uncritical jobs
  if (cnt == 0 || cnt == 32768) { // approximately every 17,5 minutes
    getUptime();
    for (uint8_t r = 0; r < NUM_RADIO; r++) {
      musterDecs[r].reset();
      FiFos[r].flush();
    }
  }
  cnt++;
  sei();
}


void setup() {
  Serial.begin(BAUDRATE);
  while (!Serial) {
    delay(90); // wait for serial port to connect. Needed for native USB
  }

  for (uint8_t r = 0; r < NUM_RADIO; r++) {
    Servers[r] = new WiFiServer(RADIO_PORT_BASE + r);
    lastTimes[r] = micros();
    AfcEnableds[r] = true;
  }
  
#if defined(ESP8266)
  gotIpEventHandler = WiFi.onStationModeGotIP([](__attribute__((unused)) const WiFiEventStationModeGotIP& event)
  {
    for (uint8_t r = 0; r < NUM_RADIO; r++) {
      if (!radioUsed[r]) continue;
      Servers[r]->begin();  // start telnet server
      Servers[r]->setNoDelay(true); // With nodelay set to true, this function will to disable Nagle algorithm (https://en.wikipedia.org/wiki/Nagle%27s_algorithm).
    }
  });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected & event)
  {
    for (uint8_t r = 0; r < NUM_RADIO; r++) {
      if (radioUsed[r]) Servers[r]->stop();
    }
    Serial.print(F("WiFi lost connection. Reason: "));
    Serial.println(event.reason);
  });
#elif defined(ESP32)
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    for (uint8_t r = 0; r < NUM_RADIO; r++) {
      if (!radioUsed[r]) continue;
      Servers[r]->begin();  // start telnet server
      Servers[r]->setNoDelay(true);
    }
  }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    for (uint8_t r = 0; r < NUM_RADIO; r++) {
      if (radioUsed[r]) Servers[r]->stop();  // end telnet server
    }
    Serial.print("WiFi lost connection. Reason: ");
    Serial.println(info.wps_fail_reason);
    
  }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif

//ESP.wdtEnable(2000);
#ifdef ESP32
  blinksos_args.callback = sosBlink;
  blinksos_args.dispatch_method = ESP_TIMER_TASK;
  blinksos_args.name = "blinkSOS";
  blinksos_args.arg = (void *)boot_sequence;
  esp_timer_create(&blinksos_args, &blinksos_handle);
  esp_timer_start_periodic(blinksos_handle, 300000);
#elif defined(ESP8266)
  os_timer_setfn(&blinksos, &sosBlink, (void *)boot_sequence);
  os_timer_arm(&blinksos, 300, true);
#endif

  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  pinMode(PIN_LED, OUTPUT);
  eeBegin();
  radioLoadPins();
  for (uint8_t r = 0; r < NUM_RADIO; r++) {
    if (radioPinReceive[r] != RADIO_NO_PIN) pinMode(radioPinReceive[r], INPUT);
  }

#ifdef CMP_CC1101
  cc1101::setup(); // set pins for input/output, init SPI (all radios)
#endif

for (curRadio = 0; curRadio < NUM_RADIO; curRadio++) {
#ifdef CMP_CC1101
  const bool csSet = radioCsPin[curRadio] != RADIO_NO_PIN;
  if (curRadio > 0 && (!csSet || radioPinReceive[curRadio] == RADIO_NO_PIN || !cc1101::checkCC1101())) {    // radio 2-4 only if a cc1101 is connected
    continue;
  }
#endif
  radioUsed[curRadio] = true;
  initEEPROM();

#ifdef CMP_CC1101
  if (csSet) {
    cc1101::CCinit();                              // CC1101 init
    hasCC1101 = cc1101::checkCC1101();             // Check for cc1101
  }

  if (hasCC1101 && EEPROM.read(EEADR(EE_CC_OFF)) == 1) {  // module disabled via web config
    cc1101::setIdleMode();
    cc1101::cmdStrobe(0x39);                       // SPWD, power down
    hasCC1101 = false;
    ccDisabled = true;
  }

  if (hasCC1101)
  {
    DBG_PRINT(FPSTR(TXT_CC1101));
    DBG_PRINT(curRadio + 1);
    DBG_PRINT(' ');
    DBG_PRINTLN(FPSTR(TXT_FOUND));
    musterDec.setCallback(&cc1101::getRSSI);   // Provide the RSSI Callback
  }
#endif 
}
curRadio = 0;


  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConfigPortalTimeoutCallback(restart);
  wifiManager.setConnectTimeout(60); // sets timeout for which to attempt connecting, useful if you get a lot of failed connects
  wifiManager.setConnectRetries(3);  // default 1
  String hostName = F("SIGNAL-");
  hostName += WiFi.getHostname();
  hostName.toUpperCase(); // SIGNAL-ESP32-B06CD4, SIGNAL-ESP-DB7D13
  wifiManager.setHostname(hostName); // set a custom hostname, sets sta and ap dhcp client id for esp32, and sta for esp8266
  wifiManager.setTitle(hostName); // set the webapp title, default WiFiManager
  wifiManager.setRemoveDuplicateAPs(false); // You can also remove or show duplicate networks (default is remove). Use this function to show (or hide) all networks.
  wifiManager.setSaveConfigCallback(saveConfigCallback); // called when wifi settings have been changed and connection was successful ( or setBreakAfterConfig(true) )

  // set static ip
  if (EEPROM.read(EE_IP) != 0 && EEPROM.read(EE_IP) != 255) {
    IPAddress ip = EEPROM_read_ipaddress(EE_IP);
    IPAddress gateway = EEPROM_read_ipaddress(EE_GW);
    IPAddress subnet = EEPROM_read_ipaddress(EE_SNM);
    wifiManager.setSTAStaticIPConfig(ip, gateway, subnet); // set static ip, gw, sn
  }
  wifiManager.setShowStaticFields(true);

  // set custom html menu content, inside menu item custom
  std::vector<const char *> menu = {"wifi", "info", "custom", "param", "sep", "update", "restart"};
  wifiManager.setMenu(menu);
  const char* menuhtml = "<form action='/sd' method='get'><button>SIGNAL-ESP Konfiguration</button></form><br/><form action='/custom' method='get'><button>SIGNAL-ESP Status</button></form><br/>";
  wifiManager.setCustomMenuHTML(menuhtml);
  wifiManager.setWebServerCallback(bindServerCallback);

  if (EEPROM.read(EE_WM_THEME)) { // set dark theme
    wifiManager.setClass("invert");
    new (&custom_field) WiFiManagerParameter(THEME_STR[1]);
  } else { // set light theme
    wifiManager.setClass("");
    new (&custom_field) WiFiManagerParameter(THEME_STR[0]);
  }
  wifiManager.addParameter(&custom_field);
  wifiManager.setSaveParamsCallback(saveParamCallback);

  wifiManager.autoConnect(hostName.c_str(), WM_AP_PASSWORD); // hostname, password ("" = open AP, else min. 8 chars)

  wifiManager.setConfigPortalBlocking(false);
  wifiManager.startWebPortal();

#ifdef ESP32
  cronTimer_args.callback = cronjob;
  cronTimer_args.name = "cronTimer";
  cronTimer_args.dispatch_method = ESP_TIMER_TASK;
  esp_timer_create(&cronTimer_args, &cronTimer_handle);
#elif defined(ESP8266)
  os_timer_disarm(&cronTimer);
  os_timer_setfn(&cronTimer, &cronjob, NULL);
#endif

for (curRadio = 0; curRadio < NUM_RADIO; curRadio++) {
if (!radioUsed[curRadio]) continue;
musterDec.setCallback(writeCallback);

#ifdef CMP_CC1101
  if (ccDisabled) {
    DBG_PRINTLN(F("cc1101 disabled"));
  } else if (!hasCC1101 || cc1101::regCheck()) {
#endif
    enableReceive();
    DBG_PRINTLN(FPSTR(TXT_RECENA));
#ifdef CMP_CC1101
  }
  else {
    DBG_PRINT(FPSTR(TXT_CC1101));
    DBG_PRINT(FPSTR(TXT_DOFRESET));
    DBG_PRINTLN(FPSTR(TXT_COMMAND));
  }
#endif

MSG_PRINTER.setTimeout(400);
if (PIN_SEND != RADIO_NO_PIN) pinAsOutput(PIN_SEND);
#ifdef CMP_CC1101
  if (wmbus == 1 && hasCC1101) { // WMBus
    mbus_init((uint8_t)wmbus_t + 1); // WMBus mode S or T
  }
#endif
}
curRadio = 0;

#ifdef ESP32
  esp_timer_start_periodic(cronTimer_handle, 32001);
  esp_timer_stop(blinksos_handle);
#elif defined(ESP8266)
  os_timer_arm(&cronTimer, 32, true);
  os_timer_disarm(&blinksos);
#endif

digitalLow(PIN_LED);
}



void loop() {
  wifiManager.process();
  webConfigLoop();

  static int aktVal = 0;
  bool state;
  for (curRadio = 0; curRadio < NUM_RADIO; curRadio++) {
  if (!radioUsed[curRadio]) continue;
  serialEvent();
  ethernetEvent();

#ifdef CMP_CC1101
  if (cc1101::ccmode == 3) {                // ASK/OOK = 3 (default)
#endif
    while (FiFo.count()>0) {                // Puffer auslesen und an Dekoder uebergeben
      aktVal = FiFo.dequeue();
      state = musterDec.decode(&aktVal);
      if (state) blinkLED = true;           // LED blinken, wenn Meldung dekodiert
      if (FiFo.count()<120) yield();
    }
#ifdef CMP_CC1101
  } else {
    if (wmbus == 0) {
      cc1101::getRxFifo();                   // xFSK = 0
    } else {
      mbus_task();
    }
  }
#endif
  }
  curRadio = 0;
#ifdef ESP8266
  yield();
#endif
}



//============================== Write callback =========================================

#define _USE_WRITE_BUFFER

#ifdef _USE_WRITE_BUFFER
  const size_t writeBufferSize = 512;
  size_t writeBufferCurrents[NUM_RADIO];
  uint8_t writeBuffers[NUM_RADIO][writeBufferSize];
  #define writeBufferCurrent writeBufferCurrents[curRadio]
  #define writeBuffer writeBuffers[curRadio]
#endif


size_t writeCallback(const uint8_t *buf, uint8_t len)
{
#ifdef _USE_WRITE_BUFFER
  if (!serverClient || !serverClient.connected())
    return 0;

  size_t result = 0;

  while (len > 0) {
    size_t copy = (len > writeBufferSize - writeBufferCurrent ? writeBufferSize - writeBufferCurrent : len);
    if (copy > 0)
    {
      memcpy(writeBuffer + writeBufferCurrent, buf, copy);
      writeBufferCurrent = writeBufferCurrent + copy;
    }
    // Buffer full or \n detected - force send
    if ((len == 1 && *buf == char(0xA)) || (writeBufferCurrent == writeBufferSize))
    {
      size_t byteswritten = 0;
      if (serverClient && serverClient.connected()) {
        byteswritten = serverClient.write(writeBuffer, writeBufferCurrent);
      }

      if (byteswritten < writeBufferCurrent) {
        memmove(writeBuffer, writeBuffer + byteswritten, writeBufferCurrent - byteswritten);
        writeBufferCurrent -= byteswritten;
      } else {
        writeBufferCurrent = 0;
      }
      result += byteswritten;
    }

    // buffer full
    len = len - copy;
    if (len > 0)
    {
      memmove((void*)buf, buf + copy, len);
    }
  }
  return len;

#else

  while (!serverClient.accept()) {
    yield();
    if (!serverClient.connected()) return 0;
  }
  DBG_PRINTLN("Called writeCallback");

  memccpy()

  return serverClient.write(buf, len);
  //serverClient.write("test");
#endif
}


inline void ethernetEvent()
{
  //check if there are any new clients
  WiFiServer &Server = *Servers[curRadio];
  if (Server.hasClient()) {
    if (!serverClient || !serverClient.connected()) {
      if (serverClient) serverClient.stop();
      serverClient = Server.accept();
      #ifdef ESP32
        #if ESP_IDF_VERSION_MAJOR < 5 // PLATFORMIO
          serverClient.flush(); // Platformio 'class WiFiClient' has no member named 'clear'
        #else
          serverClient.clear(); // Arduino warning: 'virtual void NetworkClient::flush()' is deprecated: Use clear() instead. [-Wdeprecated-declarations]
        #endif
      #elif defined(ESP8266)
        serverClient.flush();
      #endif
    } else {
      WiFiClient rejectClient = Server.accept();
      rejectClient.stop();
    }
  }

  if(serverClient && !serverClient.connected())
  {
    serverClient.stop();
  }
}



//================================= Serielle verarbeitung ======================================
void serialEvent()
{
  static uint8_t idxs[NUM_RADIO];  // per radio
  uint8_t &idx = idxs[curRadio];
  while (MSG_PRINTER.connected() > 0 && MSG_PRINTER.available())
  {
    if (idx == 14) {
      // Short buffer is now full
      MSG_PRINT(F("Command to long: "));
      MSG_PRINTLN(IB_1);
      idx = 0;
      return;
    } else {
      IB_1[idx] = (char)MSG_PRINTER.read();
      // DBG_PRINTLN(idx);
      // DBG_PRINT(IB_1[idx]);

      switch (IB_1[idx])
      {
        case '\n':
        case '\r':
        case '\0':
        case '#':

          #if defined(ESP32)
            //esp_task_wdt_reset(); // 13:25:39.758 -> E (87388) task_wdt: esp_task_wdt_reset(705): task not found
            //yield();
          #elif defined(ESP8266)
            wdt_reset();
          #endif

          if (idx > 0) {
            // DBG_PRINT("HSC");
            commands::HandleShortCommand();  // Short command received and can be processed now
          }
          idx = 0;
          return; //Exit function
        case ';':
          DBG_PRINT("send cmd detected ");
          // DBG_PRINTLN(idx);
          IB_1[idx + 1] = '\0';
          if (idx > 0)
            send_cmd();
          idx = 0; // increments to 1
          return; //Exit function
      }
      idx++;
    }
    #ifdef ESP8266
      yield();
    #endif
  }
}



int freeRam() {
  #ifdef ESP32
    return ESP.getFreeHeap();
  #elif defined(ESP8266)
    return system_get_free_heap_size();
  #endif
}



#endif  // END, defined (ESP32) || defined(ESP8266)
