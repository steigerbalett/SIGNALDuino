#pragma once

/*
 * Multi radio support (ESP32 up to 4 CC1101)
 *
 * All per radio state is stored in arrays. The firmware works on the "current radio" (curRadio),
 * the old global names (musterDec, FiFo, hasCC1101, ...) are mapped to the array element of the current radio.
 * Every radio has its own telnet port (23 + radio number), its own decoder, FIFO, interrupt and EEPROM configuration,
 * so FHEM sees every CC1101 as its own SIGNALduino.
 *
 * Pinout ESP32 (compatible to SIGNALESP32 board from Ralf9, radio 1 = former single CC1101)
 *   radio  port  CS  GDO0(TX)  GDO2(RX)
 *   1      23     5   4        13
 *   2      24    27   26       25
 *   3      25    22   -        14
 *   4      26    33   -        21
 * Radios without GDO0 can receive ASK/OOK and xFSK, but only send xFSK (FIFO), no WMBus.
 */

#if defined(ESP32) || defined(ESP8266)

#include <stdint.h>
#include <Arduino.h>   // parse core / ESP-IDF headers before the name mapping macros below are defined

#define RADIO_CTX

#ifndef NUM_RADIO
  #if defined(ESP32) && defined(CMP_CC1101)
    #define NUM_RADIO          4
  #else
    #define NUM_RADIO          1
  #endif
#endif

#define RADIO_NO_PIN           0xFF
#define RADIO_PORT_BASE        23

extern uint8_t curRadio;

// default pins, on ESP32 they can be changed in the web configuration (stored in EEPROM, EE_PIN_BASE)
#if NUM_RADIO > 1
  #ifndef RADIO_CS_PINS
    #define RADIO_CS_PINS      {5, 27, 22, 33}
  #endif
  #ifndef RADIO_SEND_PINS
    #define RADIO_SEND_PINS    {4, 26, RADIO_NO_PIN, RADIO_NO_PIN}
  #endif
  #ifndef RADIO_RECEIVE_PINS
    #define RADIO_RECEIVE_PINS {13, 25, 14, 21}
  #endif
#else
  static const uint8_t RADIO_DEF_SEND = PIN_SEND;        // keep values, PIN_SEND / PIN_RECEIVE are redefined below
  static const uint8_t RADIO_DEF_RECEIVE = PIN_RECEIVE;
  #ifdef SS
    #define RADIO_CS_PINS      {SS}
  #else
    #define RADIO_CS_PINS      {RADIO_NO_PIN}
  #endif
  #define RADIO_SEND_PINS      {RADIO_DEF_SEND}
  #define RADIO_RECEIVE_PINS   {RADIO_DEF_RECEIVE}
#endif
#ifndef RADIO_SPI_PINS
  #define RADIO_SPI_PINS       {SCK, MISO, MOSI}
#endif

#ifdef ESP32
  #define RADIO_PIN_CFG                    // pins configurable via web configuration
  #define EE_PIN_BASE          0x460       // after radio blocks: marker, SCK, MISO, MOSI, NUM_RADIO * (CS, GDO0, GDO2)
  #define EE_PIN_MARK          0xA7
#endif

extern uint8_t radioCsPin[NUM_RADIO];
extern uint8_t radioPinSend[NUM_RADIO];
extern uint8_t radioPinReceive[NUM_RADIO];
extern uint8_t radioSpiPin[3];             // SCK, MISO, MOSI

#undef PIN_SEND
#undef PIN_RECEIVE
#define PIN_SEND               radioPinSend[curRadio]
#define PIN_RECEIVE            radioPinReceive[curRadio]

// per radio state, defined in signalesp.h / cc1101.cpp
extern bool hasCC1101s[NUM_RADIO];
extern bool ccDisableds[NUM_RADIO];
extern bool AfcEnableds[NUM_RADIO];
extern bool wmbuses[NUM_RADIO];
extern bool wmbus_ts[NUM_RADIO];
extern char IB_1s[NUM_RADIO][14];
extern volatile unsigned long lastTimes[NUM_RADIO];
namespace cc1101 {
  extern uint8_t ccmodes[NUM_RADIO];
  extern uint8_t revisions[NUM_RADIO];
  extern int8_t freqOffAccs[NUM_RADIO];
  extern float freqErrAvgs[NUM_RADIO];
}

#define hasCC1101              hasCC1101s[curRadio]
#define ccDisabled             ccDisableds[curRadio]
#define AfcEnabled             AfcEnableds[curRadio]
#define wmbus                  wmbuses[curRadio]
#define wmbus_t                wmbus_ts[curRadio]
#define IB_1                   IB_1s[curRadio]
#define lastTime               lastTimes[curRadio]
#define ccmode                 ccmodes[curRadio]
// revision -> revisions[curRadio] is only mapped inside cc1101.cpp, the name clashes with ESP-IDF (esp_chip_info_t::revision)
#define freqOffAcc             freqOffAccs[curRadio]
#define freqErrAvg             freqErrAvgs[curRadio]
// declared where the types are known: musterDecs (functions.h), FiFos (functions.h), serverClients (output.h)
#define musterDec              musterDecs[curRadio]
#define FiFo                   FiFos[curRadio]
#define serverClient           serverClients[curRadio]

// EEPROM: radio 1 uses the original addresses, radio 2-4 a block of 0x40 bytes each
// block: 0x00-0x01 magic, 0x02-0x2A cc1101 register, 0x30-0x37 patable, 0x3D active bank, 0x3E module off, 0x3F features
#ifndef EE_RADIO_BASE
  #define EE_RADIO_BASE        0x3A0
#endif
#define EE_RADIO_SIZE          0x40

inline uint16_t radioEE(uint16_t a) {
  if (curRadio == 0) return a;
  uint16_t base = EE_RADIO_BASE + (curRadio - 1) * EE_RADIO_SIZE;
  if (a < 0x38) return base + a;        // magic, register, patable
  if (a == 0xFF) return base + 0x3F;     // addr_features
  if (a == 0x111) return base + 0x3E;    // EE_CC_OFF
  if (a == 0x112) return base + 0x3D;    // EE_BANK_ACT
  return a;                              // global
}
#define EEADR(a)               radioEE(a)

#else
  #define EEADR(a)             (a)
#endif
