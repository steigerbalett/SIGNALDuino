#pragma once

/*
 * Minimalistic web configuration for SIGNALESP (ESP32 / ESP8266)
 *
 * - uses the already running WiFiManager web server (no extra server, no extra RAM)
 * - CC1101 settings: module on/off, frequency, bWidth, rAmpl, sens, data rate, deviation,
 *   modulation, PA (TX power), raw register writes
 * - decoder settings: MS, MU, MC, Mred, AFC, WMBus, WMBus_T
 * - EE_BANK_COUNT banks for complete CC1101 configurations (registers + patable + decoder flags)
 * - OTA firmware update (/update, /u)
 * - system: module names, frequency offset, MC min bit length, LED, hostname, web server timeout,
 *   user / password (ESP32), backup / restore of the configuration (ESP32)
 * - the web server is stopped after the configured timeout (default WEB_TIMEOUT_MS), unless the configuration
 *   page was opened. Once opened, it stays active until "Speichern & Neustart".
 */

#if defined (ESP32) || defined(ESP8266)

#include "WiFiManager.h"

extern WiFiManager wifiManager;
extern bool radioUsed[NUM_RADIO];

#ifndef WEB_TIMEOUT_MS
  #define WEB_TIMEOUT_MS       300000UL  // 5 minutes, default of the setting EE_SET_WEBTO
#endif

#define WS wifiManager.server

bool webKeepOpen = false;               // set as soon as the config page has been opened
bool webPortalOn = true;
uint32_t webRestartAt = 0;              // restart requested (millis), done in loop after the response is sent

const char WEB_SEL[] PROGMEM = " selected";
const char WEB_CHK[] PROGMEM = " checked";

// ------------------------------- settings (EE_SET_*, radio.h) -------------------------------

String settingStr(uint16_t adr, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    const char c = EEPROM.read(adr + i);
    if (c == 0 || c == (char)0xFF) break;
    s += c;
  }
  return s;
}

static void settingWriteStr(uint16_t adr, uint8_t len, const String &s) {
  for (uint8_t i = 0; i < len; i++) EEPROM.write(adr + i, i < s.length() && i < len - 1 ? s[i] : 0);
}

void settingsInit() {                   // defaults on first start, load LED
  if (EEPROM.read(EE_SET_BASE) != EE_SET_MARK) {
    for (uint16_t a = EE_SET_BASE + 1; a < EE_SET_END; a++) EEPROM.write(a, 0);
    EEPROM.write(EE_SET_LED, PIN_LED);
    EEPROM.write(EE_SET_LEDINV, radioLedInv);
    EEPROM.write(EE_SET_WEBTO, WEB_TIMEOUT_MS / 60000UL);
    settingWriteStr(EE_SET_USER, EE_SET_USERLEN, F("admin"));
    EEPROM.write(EE_SET_BASE, EE_SET_MARK);
    EEPROM.commit();
  }
  radioLedPin = EEPROM.read(EE_SET_LED);
  radioLedInv = EEPROM.read(EE_SET_LEDINV) == 1;
}

static String radioName(uint8_t r) {
  String n = settingStr(EE_SET_NAME + r * EE_SET_NAMELEN, EE_SET_NAMELEN);
  if (!n.length()) { n = F("Modul "); n += r + 1; }
  return n;
}

static String webClean(String s, bool host) {   // allowed characters for names / hostname
  String o;
  for (uint16_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (isalnum(c) || c == '-' || (!host && (c == ' ' || c == '_' || c == '.'))) o += c;
  }
  return o;
}

static void webOpt(String &p, const String &val, const String &txt, bool sel) {
  p += F("<option value='"); p += val; p += '\'';
  if (sel) p += FPSTR(WEB_SEL);
  p += '>'; p += txt; p += F("</option>");
}

static void webCheck(String &p, const char *name, const __FlashStringHelper *txt, bool chk) {
  p += F("<label><input type='checkbox' name='"); p += name; p += '\'';
  if (chk) p += FPSTR(WEB_CHK);
  p += F("> "); p += txt; p += F("</label> ");
}

static bool webArg(const char *name) {
  return WS->hasArg(name);
}

#ifdef CMP_CC1101

static inline uint8_t ccEE(uint8_t reg) {
  return EEPROM.read(EEADR(EE_CC1101_CFG + reg));
}

static void ccSet(uint8_t reg, uint8_t val) {   // CC1101 register address, write EEPROM + chip
  if (reg >= EE_CC1101_CFG_SIZE) return;
  EEPROM.write(EEADR(EE_CC1101_CFG + reg), val);
  if (hasCC1101) cc1101::writeReg(reg, val);
  if (reg == 0x12) cc1101::ccmode = (val & 0x70) >> 4;
}

static void ccSetBWidth(uint8_t val) {          // same logic as command W12xx
  ccSet(0x10, val);
  if (ccEE(0x00) == 0x0D) {                     // ASK/OOK, DN022 optimum register settings
    ccSet(0x21, val >= 0xC7 ? 0x56 : 0xB6);     // FREND1, bWidth <= 101 kHz
    ccSet(0x03, val >= 0x57 ? 0x47 : 0x07);     // FIFOTHR, bWidth <= 325 kHz
  }
}

static float ccFreq() {
  uint32_t f = ((uint32_t)ccEE(0x0D) << 16) | ((uint16_t)ccEE(0x0E) << 8) | ccEE(0x0F);
  return f * 26.0f / 65536.0f;
}

static uint16_t ccBw(uint8_t i) {               // i = (E << 2) | M of MDMCFG4[7:4]
  return 26000UL / (8UL * (4 + (i & 3)) << (i >> 2));
}

static float ccDrate() {                        // kBaud
  return (256 + ccEE(0x11)) * (float)(1UL << (ccEE(0x10) & 0x0F)) * 26000.0f / 268435456.0f;
}

static float ccDev() {                          // kHz
  uint8_t d = ccEE(0x15);
  return 26000.0f / 131072.0f * (8 + (d & 7)) * (1 << ((d >> 4) & 7));
}

static const uint8_t rAmplTab[] PROGMEM = {24, 27, 30, 33, 36, 38, 40, 42};
static const uint8_t pa433[] PROGMEM = {0x34, 0x68, 0x60, 0x84, 0xC8, 0xC0};  // -10, -5, 0, 5, 7, 10 dBm
static const uint8_t pa868[] PROGMEM = {0x27, 0x67, 0x50, 0x81, 0xCB, 0xC2};
static const int8_t paDbm[] PROGMEM = {-10, -5, 0, 5, 7, 10};

static const char * const modTxt[] = {"2-FSK", "GFSK", "", "ASK/OOK", "4-FSK", "", "", "MSK"};

// rfmode presets, register values from FHEM (RFD-FHEM/RFFHEM, lib/FHEM/Devices/SIGNALduino/SD_Protocols/Data.pm)
// applied on top of the factory settings (like FHEM attr rfmode after SlowRF), format: count, count * (register, value)
static const uint8_t rfPresetReg[] PROGMEM = {
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x41, 0x04,0x2D, 0x05,0xD4, 0x06,0x05, 0x07,0x80, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x89, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x56, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Lacrosse_mode1
  26, 0x00,0x01, 0x02,0x46, 0x03,0x07, 0x04,0x2D, 0x05,0xD4, 0x06,0xFF, 0x07,0x00, 0x08,0x02, 0x0D,0x21, 0x0E,0x6B, 0x0F,0xD0, 0x10,0x88, 0x11,0x0B, 0x12,0x06, 0x13,0x22, 0x14,0xF8, 0x15,0x53, 0x17,0x00, 0x18,0x18, 0x19,0x16, 0x1B,0x43, 0x1C,0x68, 0x1D,0x91, 0x23,0xED, 0x25,0x17, 0x26,0x11,  // PCA301
  40, 0x00,0x01, 0x01,0x2E, 0x02,0x46, 0x03,0x04, 0x04,0xAA, 0x05,0x54, 0x06,0x0F, 0x07,0xE0, 0x08,0x00, 0x09,0x00, 0x0A,0x00, 0x0B,0x06, 0x0C,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x97, 0x11,0x83, 0x12,0x16, 0x13,0x63, 0x14,0xB9, 0x15,0x47, 0x16,0x07, 0x17,0x0C, 0x18,0x29, 0x19,0x36, 0x1A,0x6C, 0x1B,0x07, 0x1C,0x40, 0x1D,0x91, 0x1E,0x87, 0x1F,0x6B, 0x20,0xF8, 0x21,0x56, 0x22,0x11, 0x23,0xEF, 0x24,0x0A, 0x25,0x3D, 0x26,0x1F, 0x27,0x41,  // KOPP_FC
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x41, 0x04,0x2D, 0x05,0xD4, 0x06,0x05, 0x07,0x80, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0xC8, 0x11,0x83, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x42, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Lacrosse_mode2
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x43, 0x04,0x2D, 0x05,0xD4, 0x06,0x0E, 0x07,0x80, 0x08,0x00, 0x0D,0x10, 0x0E,0xB0, 0x0F,0x71, 0x10,0xA9, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x43, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Fine_Offset_WH51_434
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x43, 0x04,0x2D, 0x05,0xD4, 0x06,0x0E, 0x07,0x80, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0xE8, 0x10,0xA9, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x43, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Fine_Offset_WH51_868
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x46, 0x04,0x2D, 0x05,0xD4, 0x06,0x1A, 0x07,0xC0, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x88, 0x11,0x4C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x51, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Bresser_5in1
  23, 0x00,0x07, 0x02,0x2E, 0x03,0x02, 0x04,0xD3, 0x05,0x91, 0x06,0x0C, 0x07,0x88, 0x08,0x05, 0x0D,0x10, 0x0E,0xB0, 0x0F,0x71, 0x10,0xC8, 0x11,0x93, 0x12,0x13, 0x13,0x22, 0x14,0xF8, 0x15,0x35, 0x17,0x0F, 0x19,0x16, 0x1B,0x43, 0x1C,0x40, 0x21,0x56, 0x22,0x11,  // Rojaflex
  23, 0x00,0x01, 0x02,0x46, 0x03,0x01, 0x04,0x08, 0x05,0x69, 0x06,0xFF, 0x07,0x80, 0x08,0x02, 0x0D,0x10, 0x0E,0xAA, 0x0F,0x56, 0x10,0x8A, 0x11,0xF8, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x51, 0x19,0x16, 0x1B,0x43, 0x1C,0x40, 0x20,0xFB, 0x21,0x56, 0x22,0x11,  // Avantek
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x44, 0x04,0x2D, 0x05,0xD4, 0x06,0x12, 0x07,0xC0, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x88, 0x11,0x4C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x51, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Bresser_6in1
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x43, 0x04,0x2D, 0x05,0xD4, 0x06,0x09, 0x07,0x80, 0x08,0x00, 0x0D,0x10, 0x0E,0xB0, 0x0F,0x71, 0x10,0xA9, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x43, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Fine_Offset_WH57_434
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x43, 0x04,0x2D, 0x05,0xD4, 0x06,0x09, 0x07,0x80, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0xE8, 0x10,0xA9, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x43, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Fine_Offset_WH57_868
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x45, 0x04,0x2D, 0x05,0xD4, 0x06,0x17, 0x07,0xC0, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x88, 0x11,0x4C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x51, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Bresser_7in1
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x44, 0x04,0x2D, 0x05,0xD4, 0x06,0x12, 0x07,0xC0, 0x08,0x00, 0x0D,0x10, 0x0E,0xB0, 0x0F,0x71, 0x10,0xC8, 0x11,0x93, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x34, 0x19,0x16, 0x1B,0x43, 0x1C,0x48,  // Inkbird_IBS-P01R
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x42, 0x04,0x2D, 0x05,0xD4, 0x06,0x0B, 0x07,0x80, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0xE8, 0x10,0xA9, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x43, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Fine_Offset_WH31_868
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x43, 0x04,0x2D, 0x05,0xD4, 0x06,0x09, 0x07,0x80, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0xE8, 0x10,0xA9, 0x11,0x5C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x43, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Fine_Offset_WH40_868
  20, 0x00,0x01, 0x02,0x2E, 0x03,0x42, 0x04,0x2D, 0x05,0xD4, 0x06,0x0A, 0x07,0xC0, 0x08,0x00, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x88, 0x11,0x4C, 0x12,0x02, 0x13,0x22, 0x14,0xF8, 0x15,0x51, 0x19,0x16, 0x1B,0x43, 0x1C,0x68,  // Bresser_lightning
  23, 0x00,0x06, 0x02,0x00, 0x03,0x40, 0x04,0x76, 0x05,0x96, 0x06,0xFF, 0x07,0x04, 0x08,0x02, 0x0B,0x08, 0x0D,0x21, 0x0E,0x65, 0x0F,0x6A, 0x10,0x6A, 0x11,0x4A, 0x12,0x06, 0x13,0x22, 0x14,0xF8, 0x15,0x47, 0x19,0x2E, 0x1A,0x6D, 0x1B,0x04, 0x1C,0x09, 0x1D,0xB2,  // WMBus_S
  23, 0x00,0x06, 0x02,0x00, 0x03,0x40, 0x04,0x54, 0x05,0x3D, 0x06,0xFF, 0x07,0x04, 0x08,0x02, 0x0B,0x08, 0x0D,0x21, 0x0E,0x6B, 0x0F,0xD0, 0x10,0x5C, 0x11,0x04, 0x12,0x06, 0x13,0x22, 0x14,0xF8, 0x15,0x44, 0x19,0x2E, 0x1A,0xBF, 0x1B,0xC7, 0x1C,0x09, 0x1D,0xB2,  // WMBus_T
};
static const char rfPresetNames[] PROGMEM = "Lacrosse_mode1,PCA301,KOPP_FC,Lacrosse_mode2,Fine_Offset_WH51_434,Fine_Offset_WH51_868,Bresser_5in1,Rojaflex,Avantek,Bresser_6in1,Fine_Offset_WH57_434,Fine_Offset_WH57_868,Bresser_7in1,Inkbird_IBS-P01R,Fine_Offset_WH31_868,Fine_Offset_WH40_868,Bresser_lightning,WMBus_S,WMBus_T";   // separated by ','

static String rfPresetName(uint8_t n) {        // name n of rfPresetNames, "" if not found
  String names = FPSTR(rfPresetNames);
  int16_t st = 0;
  for (uint8_t i = 0; i < n && st >= 0; i++) {
    st = names.indexOf(',', st);
    if (st >= 0) st++;
  }
  if (st < 0) return String();
  int16_t e = names.indexOf(',', st);
  return names.substring(st, e < 0 ? names.length() : e);
}

static void rfPresetLoad(uint8_t n) {          // factory settings + preset registers, WMBus on for WMBus_S / WMBus_T
  const String name = rfPresetName(n);
  if (!name.length()) return;
  uint16_t i = 0;
  for (uint8_t k = 0; k < n; k++) i += 1 + 2 * pgm_read_byte(&rfPresetReg[i]);
  cc1101::ccFactoryReset();
  const uint8_t cnt = pgm_read_byte(&rfPresetReg[i++]);
  for (uint8_t k = 0; k < cnt; k++, i += 2) {
    const uint8_t reg = pgm_read_byte(&rfPresetReg[i]);
    if (reg < EE_CC1101_CFG_SIZE) EEPROM.write(EEADR(EE_CC1101_CFG + reg), pgm_read_byte(&rfPresetReg[i + 1]));
  }
  if (ccEE(0x0D) >= 0x20) EEPROM.write(EEADR(EE_CC1101_PA + 1), 0x81);     // 868 MHz: 5 dBm
  EEPROM.write(EEADR(EE_BANK_ACT), 0xFF);
  EEPROM.commit();
  wmbus = name.startsWith(F("WMBus_"));
  wmbus_t = (name == F("WMBus_T"));
  if (hasCC1101) cc1101::CCinit();
  storeFunctions(musterDec.MSenabled, musterDec.MUenabled, musterDec.MCenabled, musterDec.MredEnabled, AfcEnabled, wmbus, wmbus_t);  // incl. commit, WMBus init
}

// ------------------------------- banks -------------------------------
// bank layout: [0] marker, [1..0x29] cc1101 regs, [0x2A..0x31] patable, [0x32] features, [0x33..0x3F] name

static inline uint16_t bankAdr(uint8_t b) { return EE_BANK_BASE + b * EE_BANK_SIZE; }

static bool bankValid(uint8_t b) { return EEPROM.read(bankAdr(b)) == EE_BANK_MARK; }

static String bankName(uint8_t b) {
  String n;
  uint16_t a = bankAdr(b) + 0x33;
  for (uint8_t i = 0; i < EE_BANK_NAMELEN; i++) {
    char c = EEPROM.read(a + i);
    if (c == 0) break;
    n += c;
  }
  return n;
}

static void bankStore(uint8_t b, const String &name) {
  uint16_t a = bankAdr(b);
  EEPROM.write(a, EE_BANK_MARK);
  for (uint8_t i = 0; i < EE_CC1101_CFG_SIZE; i++) EEPROM.write(a + 1 + i, EEPROM.read(EEADR(EE_CC1101_CFG + i)));
  for (uint8_t i = 0; i < EE_CC1101_PA_SIZE; i++) EEPROM.write(a + 0x2A + i, EEPROM.read(EEADR(EE_CC1101_PA + i)));
  EEPROM.write(a + 0x32, EEPROM.read(EEADR(addr_features)));
  for (uint8_t i = 0; i < EE_BANK_NAMELEN; i++) {
    char c = i < name.length() ? name[i] : 0;
    if (c && !isalnum(c) && c != ' ' && c != '-' && c != '_' && c != '.') c = '_';
    EEPROM.write(a + 0x33 + i, c);
  }
  EEPROM.write(EEADR(EE_BANK_ACT), b);
  EEPROM.commit();
}

static void bankLoad(uint8_t b) {
  if (!bankValid(b)) return;
  uint16_t a = bankAdr(b);
  for (uint8_t i = 0; i < EE_CC1101_CFG_SIZE; i++) EEPROM.write(EEADR(EE_CC1101_CFG + i), EEPROM.read(a + 1 + i));
  for (uint8_t i = 0; i < EE_CC1101_PA_SIZE; i++) EEPROM.write(EEADR(EE_CC1101_PA + i), EEPROM.read(a + 0x2A + i));
  EEPROM.write(EEADR(addr_features), EEPROM.read(a + 0x32));
  EEPROM.write(EEADR(EE_BANK_ACT), b);
  EEPROM.commit();
  getFunctions(&musterDec.MSenabled, &musterDec.MUenabled, &musterDec.MCenabled, &musterDec.MredEnabled, &AfcEnabled, &wmbus, &wmbus_t);
  if (hasCC1101) {
    cc1101::CCinit();
    storeFunctions(musterDec.MSenabled, musterDec.MUenabled, musterDec.MCenabled, musterDec.MredEnabled, AfcEnabled, wmbus, wmbus_t);  // AFC reset, WMBus init
  }
}

// ------------------------------- apply form -------------------------------

static void ccApplyForm() {
  String v;
  if (hasCC1101) cc1101::setIdleMode();
  v = WS->arg(F("f"));                          // frequency MHz
  if (v.length()) {
    float f = v.toFloat();
    if (f >= 299.0f && f <= 929.0f) {
      uint32_t w = (uint32_t)(f * 65536.0f / 26.0f + 0.5f);
      ccSet(0x0D, w >> 16);
      ccSet(0x0E, w >> 8);
      ccSet(0x0F, w);
    }
  }
  v = WS->arg(F("bw"));                         // bWidth index (E<<2|M)
  if (v.length()) {
    uint8_t i = v.toInt() & 0x0F;
    ccSetBWidth((i << 4) | (ccEE(0x10) & 0x0F));
  }
  v = WS->arg(F("dr"));                         // data rate kBaud
  if (v.length()) {
    float r = v.toFloat() * 1000.0f;
    if (r >= 0.6f && r <= 600000.0f) {
      uint8_t e = 0;
      while (e < 15 && r * 1048576.0f / 26000000.0f >= (2UL << e)) e++;  // 2^(e+1) * 2^20 / 26e6 > r
      int m = (int)(r * 268435456.0f / (26000000.0f * (1UL << e)) - 256.0f + 0.5f);
      if (m > 255) { m = 0; if (e < 15) e++; }
      if (m < 0) m = 0;
      ccSet(0x10, (ccEE(0x10) & 0xF0) | e);
      ccSet(0x11, m);
    }
  }
  v = WS->arg(F("dev"));                        // deviation kHz
  if (v.length()) {
    float d = v.toFloat(), best = 1e9;
    uint8_t bv = ccEE(0x15);
    for (uint8_t e = 0; e < 8; e++)
      for (uint8_t m = 0; m < 8; m++) {
        float x = 26000.0f / 131072.0f * (8 + m) * (1 << e) - d;
        if (x < 0) x = -x;
        if (x < best) { best = x; bv = (e << 4) | m; }
      }
    ccSet(0x15, bv);
  }
  v = WS->arg(F("mod"));                        // modulation
  if (v.length()) ccSet(0x12, (ccEE(0x12) & 0x8F) | ((v.toInt() & 7) << 4));
  v = WS->arg(F("ra"));                         // rAmpl index
  if (v.length()) ccSet(0x1B, (ccEE(0x1B) & 0xF8) | (v.toInt() & 7));
  v = WS->arg(F("se"));                         // sens index
  if (v.length()) ccSet(0x1D, (ccEE(0x1D) & 0xFC) | (v.toInt() & 3));
  v = WS->arg(F("reg"));                        // raw registers "RRVV RRVV ..." (cc1101 addresses)
  for (int i = 0; i + 3 < (int)v.length(); ) {
    if (isxdigit(v[i]) && isxdigit(v[i+1]) && isxdigit(v[i+2]) && isxdigit(v[i+3])) {
      uint8_t r = strtol(v.substring(i, i + 2).c_str(), nullptr, 16);
      uint8_t x = strtol(v.substring(i + 2, i + 4).c_str(), nullptr, 16);
      if (r == 0x10) ccSetBWidth(x); else ccSet(r, x);
      i += 4;
    } else {
      i++;
    }
  }
  v = WS->arg(F("pa"));                         // patable
  if (v.length()) {
    uint8_t p = strtol(v.c_str(), nullptr, 16);
    if (p != EEPROM.read(EEADR(EE_CC1101_PA + 1))) {
      for (uint8_t i = 0; i < EE_CC1101_PA_SIZE; i++) EEPROM.write(EEADR(EE_CC1101_PA + i), i == 1 ? p : 0);
      if (hasCC1101) cc1101::writePatable();
    }
  }
  EEPROM.write(EEADR(EE_CC_OFF), webArg("on") ? 0 : 1);
  if (hasCC1101) cc1101::setReceiveMode();
}
#endif // CMP_CC1101

static void webApplySettings() {         // module name, frequency offset, MC min bit length + system settings
  if (!WS->hasArg("mn")) return;
  settingWriteStr(EE_SET_NAME + curRadio * EE_SET_NAMELEN, EE_SET_NAMELEN, webClean(WS->arg(F("mn")), false));
  int16_t mb = WS->arg(F("mb")).toInt();
  if (mb < 0 || mb > 255) mb = 0;
  EEPROM.write(EE_SET_MCMBL + curRadio, mb);
  musterDec.mcMinBitLen = mb ? mb : 17;
#ifdef CMP_CC1101
  float fo = WS->arg(F("fo")).toFloat() / (26000.0f / 16384.0f);      // kHz -> FSCTRL0 steps (1.587 kHz)
  int16_t st = fo < 0 ? (int16_t)(fo - 0.5f) : (int16_t)(fo + 0.5f);
  if (st < -128) st = -128;
  if (st > 127) st = 127;
  if ((int8_t)EEPROM.read(EE_SET_FOFF + curRadio) != st) {
    EEPROM.write(EE_SET_FOFF + curRadio, (uint8_t)(int8_t)st);
    cc1101::freqOffAcc = st;
    cc1101::freqErrAvg = 0;
    if (hasCC1101) cc1101::writeReg(0x0C, (uint8_t)(int8_t)st);
  }
#endif
  settingWriteStr(EE_SET_HOST, EE_SET_HOSTLEN, webClean(WS->arg(F("hn")), true));
  int16_t to = WS->arg(F("to")).toInt();
  EEPROM.write(EE_SET_WEBTO, to < 0 ? 0 : (to > 255 ? 255 : to));
#ifdef ESP32
  String u = webClean(WS->arg(F("au")), true);
  if (u.length()) settingWriteStr(EE_SET_USER, EE_SET_USERLEN, u);
  if (webArg("ax")) {
    settingWriteStr(EE_SET_PASS, EE_SET_PASSLEN, String());            // remove password
  } else if (WS->arg(F("ap")).length()) {
    settingWriteStr(EE_SET_PASS, EE_SET_PASSLEN, WS->arg(F("ap")));
  }
#endif
}

static void webApplyForm() {
  webApplySettings();
#ifdef CMP_CC1101
  ccApplyForm();
  wmbus = webArg("wm");
  wmbus_t = webArg("wt");
  AfcEnabled = webArg("afc");
#endif
  musterDec.MSenabled = webArg("ms");
  musterDec.MUenabled = webArg("mu");
  musterDec.MCenabled = webArg("mc");
  musterDec.MredEnabled = webArg("mr");
#ifdef CMP_CC1101
  EEPROM.write(EEADR(EE_BANK_ACT), 0xFF);               // changed config, no longer equal to a bank
  storeFunctions(musterDec.MSenabled, musterDec.MUenabled, musterDec.MCenabled, musterDec.MredEnabled, AfcEnabled, wmbus, wmbus_t);  // incl. commit
#else
  storeFunctions(musterDec.MSenabled, musterDec.MUenabled, musterDec.MCenabled, musterDec.MredEnabled, AfcEnabled);
#endif
}

// ------------------------------- page -------------------------------

static void webHead(String &p) {
  p += FPSTR(HTTP_HEAD_START);
  p.replace(FPSTR(T_v), F("SIGNAL-ESP"));
  p += FPSTR(HTTP_STYLE);
  p += F("<style>.wrap{max-width:560px}select,input{width:auto;padding:4px}fieldset{margin:8px 0}td{padding:2px 6px}button{margin:4px 0}</style>");
  p += FPSTR(HTTP_HEAD_END);
  if (EEPROM.read(EE_WM_THEME)) p.replace(FPSTR(T_c), F("invert"));
}

// ------------------------------- pins (ESP32) -------------------------------
#ifdef RADIO_PIN_CFG
#define PIN_CNT (3 + NUM_RADIO * 3)     // SCK, MISO, MOSI, per radio CS, GDO0, GDO2

static bool pinOut(uint8_t p) {         // ESP32 GPIO usable as output (no flash, no UART0)
  return p == 0 || p == 2 || p == 4 || p == 5 || (p >= 12 && p <= 19) || (p >= 21 && p <= 23) || (p >= 25 && p <= 27) || p == 32 || p == 33;
}

static bool pinIn(uint8_t p) {          // GPIO 34-39 input only
  return pinOut(p) || (p >= 34 && p <= 39);
}

static void pinName(char *n, uint8_t i) {
  n[0] = 'p'; n[1] = '0' + i / 10; n[2] = '0' + i % 10; n[3] = 0;
}

static uint8_t pinCur(uint8_t i) {      // saved value (EEPROM) or active default
  if (EEPROM.read(EE_PIN_BASE) == EE_PIN_MARK) return EEPROM.read(EE_PIN_BASE + 1 + i);
  if (i < 3) return radioSpiPin[i];
  i -= 3;
  const uint8_t r = i / 3;
  return i % 3 == 0 ? radioCsPin[r] : i % 3 == 1 ? radioPinSend[r] : radioPinReceive[r];
}

static bool webSavePins() {
  uint8_t v[PIN_CNT];
  uint8_t used[PIN_CNT + 1];             // pins + LED
  uint8_t cnt = 0;
  char n[4];
  for (uint8_t i = 0; i < PIN_CNT; i++) {
    pinName(n, i);
    String s = WS->arg(n);
    s.trim();
    if (s.length() == 0 || s[0] == '-') v[i] = RADIO_NO_PIN;
    else v[i] = s.toInt() >= 0 && s.toInt() <= 39 ? s.toInt() : 254;   // 254 -> invalid
  }
  if (!pinOut(v[0]) || !pinIn(v[1]) || !pinOut(v[2])) return false;    // SCK, MISO, MOSI
  used[cnt++] = v[0]; used[cnt++] = v[1]; used[cnt++] = v[2];
  for (uint8_t r = 0; r < NUM_RADIO; r++) {
    const uint8_t cs = v[3 + r * 3], tx = v[4 + r * 3], rx = v[5 + r * 3];
    if (cs == RADIO_NO_PIN && r > 0) continue;                         // module not used
    if (cs != RADIO_NO_PIN) { if (!pinOut(cs)) return false; used[cnt++] = cs; }
    if (tx != RADIO_NO_PIN) { if (!pinOut(tx)) return false; used[cnt++] = tx; }
    if (!pinIn(rx)) return false;                                      // GDO2 needed
    used[cnt++] = rx;
  }
  String l = WS->arg(F("pl"));                                        // LED, empty = no LED
  l.trim();
  const uint8_t led = (l.length() == 0 || l[0] == '-') ? RADIO_NO_PIN : (l.toInt() >= 0 && l.toInt() <= 39 ? l.toInt() : 254);
  if (led != RADIO_NO_PIN) {
    if (!pinOut(led)) return false;
    used[cnt++] = led;
  }
  for (uint8_t i = 0; i < cnt; i++)
    for (uint8_t j = i + 1; j < cnt; j++)
      if (used[i] == used[j]) return false;                            // pin used twice
  EEPROM.write(EE_PIN_BASE, EE_PIN_MARK);
  for (uint8_t i = 0; i < PIN_CNT; i++) EEPROM.write(EE_PIN_BASE + 1 + i, v[i]);
  EEPROM.write(EE_SET_LED, led);
  EEPROM.write(EE_SET_LEDINV, webArg("li") ? 1 : 0);
  EEPROM.commit();
  return true;
}

static void webPinInput(String &p, uint8_t i) {
  char n[4];
  pinName(n, i);
  p += F("<input name='"); p += n; p += F("' size='2' value='");
  uint8_t v = pinCur(i);
  if (v != RADIO_NO_PIN) p += v;
  p += F("'>");
}

static void webPins(String &p) {
  p += F("<details><summary>Pins (GPIO, wirksam nach Neustart)</summary>SCK ");
  webPinInput(p, 0);
  p += F(" MISO ");
  webPinInput(p, 1);
  p += F(" MOSI ");
  webPinInput(p, 2);
  p += F("<table><tr><td></td><td>CS</td><td>GDO0 (TX)</td><td>GDO2 (RX)</td></tr>");
  for (uint8_t r = 0; r < NUM_RADIO; r++) {
    p += F("<tr><td>Modul "); p += r + 1; p += F("</td>");
    for (uint8_t k = 0; k < 3; k++) {
      p += F("<td>");
      webPinInput(p, 3 + r * 3 + k);
      p += F("</td>");
    }
    p += F("</tr>");
  }
  p += F("</table>LED <input name='pl' size='2' value='");
  if (EEPROM.read(EE_SET_LED) != RADIO_NO_PIN) p += EEPROM.read(EE_SET_LED);
  p += F("'> ");
  webCheck(p, "li", F("invertiert"), EEPROM.read(EE_SET_LEDINV) == 1);
  p += F("<br><small>Leer = nicht belegt (LED leer = keine LED). Modul ohne CS = nicht verwendet, ohne GDO0 = nur Empfang/xFSK. Standard-LED ");
  p += PIN_LED;
  p += F(".</small><br><button name='a' value='pd'>Standard-Pins</button></details>");
}
#endif

static void handleConfigRadio() {

  if (WS->method() == HTTP_POST) {
    String a = WS->arg(F("a"));
    bool pinErr = false;
#ifdef RADIO_PIN_CFG
    if (a == "pd") {                            // default pins
      EEPROM.write(EE_PIN_BASE, 0);
      EEPROM.write(EE_SET_LED, PIN_LED);
#ifdef PIN_LED_INVERSE
      EEPROM.write(EE_SET_LEDINV, 1);
#else
      EEPROM.write(EE_SET_LEDINV, 0);
#endif
      EEPROM.commit();
    } else if (a != "bl" && a != "fr" && a != "f8" && a != "rm" && WS->hasArg("p00")) {
      pinErr = !webSavePins();
    }
#endif
#ifdef CMP_CC1101
    uint8_t b = WS->arg(F("b")).toInt() % EE_BANK_COUNT;
    if (a == "bl") {
      bankLoad(b);
    } else if (a == "rm") {
      rfPresetLoad(WS->arg(F("rf")).toInt());
    } else if (a == "fr" || a == "f8") {      // defaults 433.92 / 868.35 MHz (SlowRF, FHEM rfmode SlowRF_ccFactoryReset)
      cc1101::ccFactoryReset();
      if (a == "f8") {
        static const uint8_t freq868[] = {0x21, 0x65, 0xE8};   // 868.350 MHz (FREQ2..0)
        for (uint8_t i = 0; i < 3; i++) EEPROM.write(EEADR(EE_CC1101_CFG + CC1101_FREQ2 + i), freq868[i]);
        EEPROM.write(EEADR(EE_CC1101_PA + 1), 0x81);          // 5 dBm for 868 MHz
      }
      EEPROM.write(EEADR(EE_BANK_ACT), 0xFF);
      EEPROM.commit();
      if (hasCC1101) cc1101::CCinit();
    } else
#endif
    {
      webApplyForm();
#ifdef CMP_CC1101
      if (a == "bs") bankStore(b, WS->arg(F("n")));
#endif
    }
    if (a == "rb" && !pinErr) {
      String p;
      webHead(p);
      p += F("<meta http-equiv='refresh' content='15;url=/sd'>Gespeichert, Neustart ...");
      p += FPSTR(HTTP_END);
      WS->send(200, FPSTR(HTTP_HEAD_CT), p);
      webRestartAt = millis() + 1000;           // restart from loop, after the response is sent
      return;
    }
    WS->sendHeader(F("Location"), WS->uri() + F("?m=") + curRadio + (pinErr ? F("&e=1") : F("")));   // PRG, avoid resubmit on reload
    WS->send(303);
    return;
  }

  String p;
  p.reserve(7000);
  webHead(p);
  p += F("<h3>SIGNAL-ESP</h3><small>" PROGVERS " | Uptime ");
  p += getUptime();
  p += F(" s</small><p>");
  for (uint8_t r = 0; r < NUM_RADIO; r++) {     // radio selection
    if (!radioUsed[r]) continue;
    if (r == curRadio) p += F("<b>"); else { p += F("<a href='?m="); p += r; p += F("'>"); }
    p += radioName(r); p += F(" (Port "); p += RADIO_PORT_BASE + r; p += ')';
    p += r == curRadio ? F("</b> ") : F("</a> ");
  }
  p += F("</p><form method='post'><button name='a' value='set' style='position:absolute;left:-9999px' tabindex='-1'></button>"  // default for enter key
         "<input type='hidden' name='m' value='");
  p += curRadio;
  p += F("'>");

#ifdef CMP_CC1101
  p += F("<fieldset><legend>CC1101 ");
  if (hasCC1101) p += F("(gefunden)"); else p += F("(nicht aktiv)");
  if (PIN_SEND == RADIO_NO_PIN) p += F(" ohne GDO0: nur Empfang + xFSK-Senden, kein WMBus");
  p += F("</legend><table><tr><td>Modul</td><td>");
  webCheck(p, "on", F("aktiv (nach Neustart)"), EEPROM.read(EEADR(EE_CC_OFF)) != 1);
  p += F("</td></tr><tr><td>Name</td><td><input name='mn' maxlength='11' size='12' value='");
  p += settingStr(EE_SET_NAME + curRadio * EE_SET_NAMELEN, EE_SET_NAMELEN);
  p += F("'></td></tr><tr><td>Frequenz MHz</td><td><input name='f' type='number' step='any' min='300' max='928' value='");
  p += String(ccFreq(), 3);
  p += F("'></td></tr><tr><td>Frequenz-Offset kHz</td><td><input name='fo' type='number' step='any' min='-203' max='201' value='");
  p += String((int8_t)EEPROM.read(EE_SET_FOFF + curRadio) * (26000.0f / 16384.0f), 1);
  p += F("'> <small>Quarzkorrektur, 1,59-kHz-Schritte</small></td></tr><tr><td>bWidth kHz</td><td><select name='bw'>");
  uint8_t bw = ccEE(0x10) >> 4;
  for (int8_t i = 15; i >= 0; i--) webOpt(p, String(i), String(ccBw(i)), i == bw);
  p += F("</select></td></tr><tr><td>rAmpl dB</td><td><select name='ra'>");
  uint8_t ra = ccEE(0x1B) & 7;
  for (uint8_t i = 0; i < 8; i++) webOpt(p, String(i), String(pgm_read_byte(&rAmplTab[i])), i == ra);
  p += F("</select></td></tr><tr><td>sens dB</td><td><select name='se'>");
  uint8_t se = ccEE(0x1D) & 3;
  for (uint8_t i = 0; i < 4; i++) webOpt(p, String(i), String(4 + i * 4), i == se);
  p += F("</select></td></tr><tr><td>Modulation</td><td><select name='mod'>");
  uint8_t md = (ccEE(0x12) >> 4) & 7;
  for (uint8_t i = 0; i < 8; i++) {
    if (modTxt[i][0]) webOpt(p, String(i), modTxt[i], i == md);
  }
  p += F("</select></td></tr><tr><td>Datenrate kBaud</td><td><input name='dr' type='number' step='any' value='");
  p += String(ccDrate(), 2);
  p += F("'></td></tr><tr><td>Deviation kHz</td><td><input name='dev' type='number' step='any' value='");
  p += String(ccDev(), 2);
  p += F("'></td></tr><tr><td>Sendeleistung</td><td><select name='pa'>");
  const uint8_t *pt = ccFreq() < 600.0f ? pa433 : pa868;
  uint8_t pa = EEPROM.read(EEADR(EE_CC1101_PA + 1));
  bool found = false;
  for (uint8_t i = 0; i < sizeof(pa433); i++) {
    uint8_t x = pgm_read_byte(&pt[i]);
    found |= x == pa;
    webOpt(p, cc1101::dec2hex(x), String((int)(int8_t)pgm_read_byte(&paDbm[i])) + F(" dBm"), x == pa);
  }
  if (!found) webOpt(p, cc1101::dec2hex(pa), String(F("0x")) + cc1101::dec2hex(pa), true);
  p += F("</select></td></tr><tr><td>Register</td><td><input name='reg' placeholder='z.B. 0D21 0E65' size='24'></td></tr></table>");
  p += F("<small>Register (Adr Wert, hex): ");
  for (uint8_t i = 0; i < EE_CC1101_CFG_SIZE; i++) {
    p += cc1101::dec2hex(ccEE(i));
    p += (i & 7) == 7 ? F(" | ") : F(" ");
  }
  p += F("</small></fieldset>");
#endif

#ifdef RADIO_PIN_CFG
  if (WS->hasArg("e")) p += F("<p style='color:red'>Ung&uuml;ltige oder doppelte Pins, nicht gespeichert!</p>");
#endif
  p += F("<fieldset><legend>Decoder</legend>");
  webCheck(p, "ms", F("MS"), musterDec.MSenabled);
  webCheck(p, "mu", F("MU"), musterDec.MUenabled);
  webCheck(p, "mc", F("MC"), musterDec.MCenabled);
  webCheck(p, "mr", F("Mred"), musterDec.MredEnabled);
#ifndef CMP_CC1101
  p += F("<input type='hidden' name='mn' value='");      // no cc1101 table: keep name
  p += settingStr(EE_SET_NAME + curRadio * EE_SET_NAMELEN, EE_SET_NAMELEN);
  p += F("'>");
#endif
  p += F("<br>MC min. Bitl&auml;nge <input name='mb' type='number' min='0' max='255' size='3' value='");
  if (EEPROM.read(EE_SET_MCMBL + curRadio)) p += EEPROM.read(EE_SET_MCMBL + curRadio);
  p += F("'> <small>leer = Standard (17)</small>");
#ifdef CMP_CC1101
  webCheck(p, "afc", F("AFC"), AfcEnabled);
  webCheck(p, "wm", F("WMBus"), wmbus);
  webCheck(p, "wt", F("WMBus T"), wmbus_t);
#endif
  p += F("</fieldset>");

#ifdef CMP_CC1101
  uint8_t act = EEPROM.read(EEADR(EE_BANK_ACT));
  p += F("<fieldset><legend>B&auml;nke</legend><select name='b'>");
  for (uint8_t i = 0; i < EE_BANK_COUNT; i++) {
    String t = String(i) + F(": ");
    if (bankValid(i)) {
      t += bankName(i);
      t += F(" (");
      // frequency of bank
      uint16_t a = bankAdr(i) + 1;
      uint32_t f = ((uint32_t)EEPROM.read(a + 0x0D) << 16) | ((uint16_t)EEPROM.read(a + 0x0E) << 8) | EEPROM.read(a + 0x0F);
      t += String(f * 26.0f / 65536.0f, 2);
      t += F(" MHz)");
    } else {
      t += '-';
    }
    if (i == act) t += F(" *");
    webOpt(p, String(i), t, i == act);
  }
  p += F("</select> Name <input name='n' maxlength='12' size='10'><br>"
         "<button name='a' value='bs'>Aktuelle Werte in Bank speichern</button> "
         "<button name='a' value='bl'>Bank laden</button></fieldset>");
#endif

  p += F("<fieldset><legend>System (alle Module)</legend><table><tr><td>Hostname</td><td><input name='hn' maxlength='31' size='20' placeholder='SIGNAL-ESP32-...' value='");
  p += settingStr(EE_SET_HOST, EE_SET_HOSTLEN);
  p += F("'> <small>nach Neustart</small></td></tr><tr><td>Webserver aus nach</td><td><input name='to' type='number' min='0' max='255' size='3' value='");
  p += EEPROM.read(EE_SET_WEBTO);
  p += F("'> min <small>0 = immer an</small></td></tr>");
#ifdef ESP32
  p += F("<tr><td>Benutzer</td><td><input name='au' maxlength='15' size='12' value='");
  p += settingStr(EE_SET_USER, EE_SET_USERLEN);
  p += F("'></td></tr><tr><td>Passwort</td><td><input name='ap' type='password' maxlength='19' size='12' autocomplete='new-password' placeholder='");
  p += settingStr(EE_SET_PASS, EE_SET_PASSLEN).length() ? F("unver&auml;ndert") : F("kein Schutz");
  p += F("'> ");
  webCheck(p, "ax", F("Passwort l&ouml;schen"), false);
  p += F("<br><small>Sch&uuml;tzt Konfiguration, Status und OTA-Update (nicht im WLAN-Setup-Modus).</small></td></tr>"
         "<tr><td>Konfiguration</td><td><a href='/bk'>Backup herunterladen</a></td></tr>");
#endif
  p += F("</table></fieldset>");

  p += F("<button name='a' value='set'>&Uuml;bernehmen</button> "
         "<button name='a' value='rb'>Speichern &amp; Neustart</button>");
#ifdef RADIO_PIN_CFG
  webPins(p);
#endif
#ifdef CMP_CC1101
  p += F(" <button name='a' value='fr' onclick=\"return confirm('Standardwerte 433 MHz laden?')\">Standard 433 MHz</button>"
         " <button name='a' value='f8' onclick=\"return confirm('Standardwerte 868 MHz laden?')\">Standard 868 MHz</button>");
  {
    p += F("<fieldset><legend>rfmode (FSK-Presets aus FHEM)</legend><select name='rf'>");
    String n;
    for (uint8_t i = 0; (n = rfPresetName(i)).length(); i++) {
      p += F("<option value='"); p += i; p += F("'>"); p += n; p += F("</option>");
    }
    p += F("</select> <button name='a' value='rm' onclick=\"return confirm('Preset laden? Aktuelle CC1101-Werte werden ersetzt.')\">Preset laden</button>"
           "<br><small>Werkseinstellungen + Register des FHEM-rfmode, WMBus wird bei WMBus_S/T aktiviert. "
           "In FHEM die passenden MN-Protokolle aktivieren, Attribut rfmode nicht zus&auml;tzlich setzen.</small></fieldset>");
  }
#endif
  p += F("</form>");
#ifdef ESP32
  p += F("<form method='post' action='/rs' enctype='multipart/form-data' onsubmit=\"return confirm('Konfiguration wiederherstellen und neu starten?')\">"
         "<fieldset><legend>Wiederherstellen</legend><input type='file' name='f' accept='.sdc'> <button>Backup einspielen</button></fieldset></form>");
#endif
  p += F("<hr><a href='/wifi'>WLAN</a> | <a href='/update'>Firmware-Update (OTA)</a> | <a href='/info'>Info</a> | <a href='/custom'>Status</a> | <a href='/param'>Theme</a>"
         "<br><small>Der Webserver bleibt bis &bdquo;Speichern &amp; Neustart&ldquo; aktiv.</small>");
  p += FPSTR(HTTP_END);
  WS->send(200, FPSTR(HTTP_HEAD_CT), p);
}

void handleConfig() {
  webKeepOpen = true;                           // page opened -> keep web server until save & reboot
  const uint8_t remRadio = curRadio;
  uint8_t m = WS->arg(F("m")).toInt();
  curRadio = (m < NUM_RADIO && radioUsed[m]) ? m : 0;
  handleConfigRadio();
  curRadio = remRadio;
}

#ifdef ESP32
/*
 * OTA update (/u), replaces the WiFiManager handler (registered first in bindServerCallback):
 * - receiving is stopped before flashing (interrupts, cron timer, CC1101 idle)
 * - the restart is done from loop after the response has been sent completely,
 *   the result page reloads the start page after the restart
 */
#include <Update.h>
extern esp_timer_handle_t cronTimer_handle;
static bool webOtaRadioStopped = false;

static void webOtaStopRadios() {
  if (webOtaRadioStopped) return;
  webOtaRadioStopped = true;
  esp_timer_stop(cronTimer_handle);
  const uint8_t remRadio = curRadio;
  for (curRadio = 0; curRadio < NUM_RADIO; curRadio++) {
    if (radioUsed[curRadio]) disableReceive();
  }
  curRadio = remRadio;
}

static void handleOtaUpload() {
  HTTPUpload &upload = WS->upload();
  if (upload.status == UPLOAD_FILE_START) {
    webKeepOpen = true;
    webOtaStopRadios();
    DBG_PRINT(F("OTA start ")); DBG_PRINTLN(upload.filename);
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!Update.hasError()) Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
  delay(0);
}

static void handleOtaDone() {
  const bool ok = !Update.hasError() && Update.isFinished();
  String p;
  webHead(p);
  p += F("<meta http-equiv='refresh' content='20;url=/'>");
  if (ok) {
    p += F("<h3>Update erfolgreich</h3>Neustart, die Seite wird in 20 s neu geladen ...");
  } else {
    p += F("<h3>Update fehlgeschlagen</h3>");
    p += Update.errorString();
    p += F("<br>Neustart mit der bisherigen Firmware ...");
  }
  p += FPSTR(HTTP_END);
  WS->sendHeader(F("Connection"), F("close"));
  WS->send(200, FPSTR(HTTP_HEAD_CT), p);
  webRestartAt = millis() + 1500;                // also after an error: receiving was stopped
}

// ------------------------------- backup / restore -------------------------------
// file: "SDC1", EE_SIZE (2 byte, little endian), EEPROM content

static const char BK_MAGIC[] = "SDC1";
static uint8_t *rsBuf = nullptr;
static uint16_t rsLen = 0;

static void handleBackup() {
  const uint16_t n = 6 + EE_SIZE;
  uint8_t *b = (uint8_t *)malloc(n);
  if (!b) { WS->send(500); return; }
  memcpy(b, BK_MAGIC, 4);
  b[4] = EE_SIZE & 0xFF;
  b[5] = EE_SIZE >> 8;
  for (uint16_t i = 0; i < EE_SIZE; i++) b[6 + i] = EEPROM.read(i);
  String host = settingStr(EE_SET_HOST, EE_SET_HOSTLEN);
  if (!host.length()) host = F("signalesp");
  WS->sendHeader(F("Content-Disposition"), String(F("attachment; filename=")) + host + F(".sdc"));
  WS->send_P(200, "application/octet-stream", (const char *)b, n);
  free(b);
}

static void handleRestoreUpload() {
  HTTPUpload &upload = WS->upload();
  if (upload.status == UPLOAD_FILE_START) {
    free(rsBuf);
    rsBuf = (uint8_t *)malloc(6 + EE_SIZE);
    rsLen = 0;
  } else if (upload.status == UPLOAD_FILE_WRITE && rsBuf) {
    for (size_t i = 0; i < upload.currentSize && rsLen < 6 + EE_SIZE + 1; i++) {
      if (rsLen < 6 + EE_SIZE) rsBuf[rsLen] = upload.buf[i];
      rsLen++;                                   // rsLen > 6 + EE_SIZE -> file too big
    }
  }
}

static void handleRestoreDone() {
  const bool ok = rsBuf && rsLen == 6 + EE_SIZE && memcmp(rsBuf, BK_MAGIC, 4) == 0 && rsBuf[4] == (EE_SIZE & 0xFF) && rsBuf[5] == (EE_SIZE >> 8);
  String p;
  webHead(p);
  if (ok) {
    for (uint16_t i = 0; i < EE_SIZE; i++) EEPROM.write(i, rsBuf[6 + i]);
    EEPROM.commit();
    p += F("<meta http-equiv='refresh' content='20;url=/'><h3>Konfiguration wiederhergestellt</h3>Neustart ...");
    webRestartAt = millis() + 1500;
  } else {
    p += F("<h3>Ung&uuml;ltige Backup-Datei</h3><a href='/sd'>Zur&uuml;ck</a>");
  }
  free(rsBuf);
  rsBuf = nullptr;
  p += FPSTR(HTTP_END);
  WS->send(200, FPSTR(HTTP_HEAD_CT), p);
}

// ------------------------------- authentication -------------------------------
// first request handler: if a password is set, every request needs basic auth (not in the WiFi setup portal),
// afterwards the request is passed to the next matching handler (WiFiManager, configuration, OTA).
// Signatures for Arduino core 2.x (String) and 3.x (const String &).

static bool webAuthOk() {
  const String pw = settingStr(EE_SET_PASS, EE_SET_PASSLEN);
  if (!pw.length() || wifiManager.getConfigPortalActive()) return true;
  return WS->authenticate(settingStr(EE_SET_USER, EE_SET_USERLEN).c_str(), pw.c_str());
}

class WebAuthHandler : public RequestHandler {
  RequestHandler *find(HTTPMethod m, const String &uri) {
    for (RequestHandler *h = next(); h; h = h->next()) {
      if (h->canHandle(m, uri)) return h;
    }
    return nullptr;
  }
  bool doHandle(WebServer &server, HTTPMethod m, const String &uri) {
    if (!webAuthOk()) {
      server.requestAuthentication(BASIC_AUTH, "SIGNAL-ESP");
      return true;
    }
    RequestHandler *h = find(m, uri);
    if (h) return h->handle(server, m, uri);
    return false;                                 // not found
  }
  void doUpload(WebServer &server, const String &uri, HTTPUpload &upload) {
    if (!webAuthOk()) return;
    RequestHandler *h = find(HTTP_POST, uri);
    if (h) h->upload(server, uri, upload);
  }
  bool needed() {
    return settingStr(EE_SET_PASS, EE_SET_PASSLEN).length() && !wifiManager.getConfigPortalActive();
  }
public:
  bool canHandle(HTTPMethod m, String uri) { (void)m; (void)uri; return needed(); }
  bool canHandle(HTTPMethod m, const String &uri) { (void)m; (void)uri; return needed(); }
  bool canUpload(String uri) { (void)uri; return needed(); }
  bool canUpload(const String &uri) { (void)uri; return needed(); }
  bool handle(WebServer &server, HTTPMethod m, String uri) { return doHandle(server, m, uri); }
  bool handle(WebServer &server, HTTPMethod m, const String &uri) { return doHandle(server, m, uri); }
  void upload(WebServer &server, String uri, HTTPUpload &upload) { doUpload(server, uri, upload); }
  void upload(WebServer &server, const String &uri, HTTPUpload &upload) { doUpload(server, uri, upload); }
};

void webOtaBind() {
  WS->addHandler(new WebAuthHandler());         // must be the first handler
  WS->on("/u", HTTP_POST, handleOtaDone, handleOtaUpload);
  WS->on("/bk", HTTP_GET, handleBackup);
  WS->on("/rs", HTTP_POST, handleRestoreDone, handleRestoreUpload);
}
#endif

// stop web server after the configured timeout (EE_SET_WEBTO minutes, 0 = never), if nobody opened the configuration page
inline void webConfigLoop() {
  if (webRestartAt && (int32_t)(millis() - webRestartAt) >= 0) {
    WS->stop();
    WiFi.disconnect(true);
    delay(100);
    ESP.restart();
  }
  const uint8_t to = EEPROM.read(EE_SET_WEBTO);
  if (webPortalOn && !webKeepOpen && to && !wifiManager.getConfigPortalActive() && millis() > to * 60000UL) {
    wifiManager.stopWebPortal();
    webPortalOn = false;
    DBG_PRINTLN(F("web server stopped"));
  }
}

#endif
