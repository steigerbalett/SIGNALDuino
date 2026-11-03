#pragma once

/*
 * Multi radio support (ESP32 up to 4 CC1101)
 *
 * All per radio state is stored in arrays. The firmware works on the "current radio" (curRadio),
 * the old global names (musterDec, FiFo, hasCC1101, ...) are mapped to the array element of the current radio.
 * Every radio has its own telnet port (23 + radio number), its own decoder, FIFO, interrupt and EEPROM configuration,
 * so FHEM sees every CC1101 as its own SIGNALduino.
 *
 * Default pinout ESP32 WROOM, as SIGNALESP32 board from Ralf9 (radio 1 = former single CC1101 = Ralf9 B, radio 2 = A, 3 = C, 4 = D)
 *   radio  port  CS  GDO0(TX)  GDO2(RX)
 *   1      23     5   4        13
 *   2      24    27   26       25
 *   3      25    17   21       22
 *   4      26    32   -        33
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
    #define RADIO_CS_PINS      {5, 27, 17, 32}
  #endif
  #ifndef RADIO_SEND_PINS
    #define RADIO_SEND_PINS    {4, 26, 21, RADIO_NO_PIN}
  #endif
  #ifndef RADIO_RECEIVE_PINS
    #define RADIO_RECEIVE_PINS {13, 25, 22, 33}
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
extern volatile uint32_t radioPulseCnt[NUM_RADIO];   // diagnostics: pulses (pin interrupts) per radio
extern volatile uint32_t radioCronCnt;              // diagnostics: cronjob runs
extern volatile uint32_t radioOvfCnt[NUM_RADIO];    // diagnostics: pulses lost, FIFO full
extern uint32_t radioSameSign[NUM_RADIO];           // diagnostics: two pulses with the same sign in a row (level read wrong / pulse lost)
extern uint32_t radioLoopRate;
extern volatile uint32_t radioTrashCnt[NUM_RADIO];  // diagnostics: pulses < pulseMin (dropped)
extern volatile uint32_t radioLevelRep[NUM_RADIO];  // diagnostics: same pin level in two interrupts in a row (edge missed)
#define RADIO_TRACE_LEN 64
extern int radioTrace[RADIO_TRACE_LEN];             // diagnostics: last pulses of radio 1 (decoder input)
extern uint8_t radioTracePos;                      // diagnostics: loop() runs per second

// one lock for pulse FIFO / lastTimes: written by the pin interrupt, the cronjob timer task (other core) and read in loop
#ifdef ESP32
  extern portMUX_TYPE radioMux;
  #define RADIO_LOCK_ISR()     portENTER_CRITICAL_ISR(&radioMux)
  #define RADIO_UNLOCK_ISR()   portEXIT_CRITICAL_ISR(&radioMux)
  #define RADIO_LOCK()         portENTER_CRITICAL(&radioMux)
  #define RADIO_UNLOCK()       portEXIT_CRITICAL(&radioMux)
  #define RADIO_PIN_HIGH(p)    (gpio_get_level((gpio_num_t)(p)) != 0)   // fast, IRAM safe (digitalRead of core 3.x checks the peripheral manager)
#else
  #define RADIO_LOCK_ISR()
  #define RADIO_UNLOCK_ISR()
  #define RADIO_LOCK()         noInterrupts()
  #define RADIO_UNLOCK()       interrupts()
  #define RADIO_PIN_HIGH(p)    (digitalRead(p) == HIGH)
#endif
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

// global settings of the web configuration, after the pin block (initialized in settingsInit, web-config.h)
#define EE_SET_BASE            0x480
#define EE_SET_MARK            0xC3
#define EE_SET_LED             (EE_SET_BASE + 0x01)  // LED pin, RADIO_NO_PIN = no LED
#define EE_SET_LEDINV          (EE_SET_BASE + 0x02)  // 1 = LED inverse
#define EE_SET_WEBTO           (EE_SET_BASE + 0x03)  // web server timeout in minutes, 0 = always on
#define EE_SET_MCMBL           (EE_SET_BASE + 0x04)  // 4 x MC min bit length, 0 = default
#define EE_SET_FOFF            (EE_SET_BASE + 0x08)  // 4 x int8 frequency offset (FSCTRL0, 1.587 kHz steps)
#define EE_SET_NAME            (EE_SET_BASE + 0x0C)  // 4 x module name
#define EE_SET_NAMELEN         12
#define EE_SET_USER            (EE_SET_BASE + 0x3C)  // web user
#define EE_SET_USERLEN         16
#define EE_SET_PASS            (EE_SET_BASE + 0x4C)  // web password, empty = no authentication
#define EE_SET_PASSLEN         20
#define EE_SET_HOST            (EE_SET_BASE + 0x60)  // hostname, empty = SIGNAL-ESP32-xxxxxx
#define EE_SET_HOSTLEN         32
#define EE_SET_END             (EE_SET_BASE + 0x80)

#define CC_FOFF_BASE           ((int8_t)EEPROM.read(EE_SET_FOFF + curRadio))   // frequency offset of the current radio

extern uint8_t radioLedPin;
extern bool radioLedInv;
extern uint32_t radioMsgCnt[NUM_RADIO];
#define RADIO_LED(on)          do { if (radioLedPin != RADIO_NO_PIN) digitalWrite(radioLedPin, (bool)(on) != radioLedInv); } while (0)
#define RADIO_MSG_CNT()        (radioMsgCnt[curRadio]++)

#else
  #define EEADR(a)             (a)
  #define CC_FOFF_BASE         0
  #define RADIO_MSG_CNT()
#endif
