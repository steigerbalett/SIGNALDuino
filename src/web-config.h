#pragma once

/*
 * Minimalistic web configuration for SIGNALESP (ESP32 / ESP8266)
 *
 * - uses the already running WiFiManager web server (no extra server, no extra RAM)
 * - CC1101 settings: module on/off, frequency, bWidth, rAmpl, sens, data rate, deviation,
 *   modulation, PA (TX power), raw register writes
 * - decoder settings: MS, MU, MC, Mred, AFC, WMBus, WMBus_T
 * - EE_BANK_COUNT banks for complete CC1101 configurations (registers + patable + decoder flags)
 * - OTA firmware update via WiFiManager (/update)
 * - the web server is stopped after WEB_TIMEOUT_MS uptime, unless the configuration page was opened.
 *   Once opened, it stays active until "Speichern & Neustart".
 */

#if defined (ESP32) || defined(ESP8266)

#include "WiFiManager.h"

extern WiFiManager wifiManager;
extern bool radioUsed[NUM_RADIO];

#ifndef WEB_TIMEOUT_MS
  #define WEB_TIMEOUT_MS       300000UL  // 5 minutes
#endif

#define WS wifiManager.server

bool webKeepOpen = false;               // set as soon as the config page has been opened
bool webPortalOn = true;

const char WEB_SEL[] PROGMEM = " selected";
const char WEB_CHK[] PROGMEM = " checked";

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

static void webApplyForm() {
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
  p += F("<style>.wrap{max-width:560px}select,input{width:auto;padding:4px}fieldset{margin:8px 0}td{padding:2px 6px}</style>");
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
  uint8_t used[PIN_CNT + 1];
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
  used[cnt++] = PIN_LED;
  for (uint8_t i = 0; i < cnt; i++)
    for (uint8_t j = i + 1; j < cnt; j++)
      if (used[i] == used[j]) return false;                            // pin used twice
  EEPROM.write(EE_PIN_BASE, EE_PIN_MARK);
  for (uint8_t i = 0; i < PIN_CNT; i++) EEPROM.write(EE_PIN_BASE + 1 + i, v[i]);
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
  p += F("</table><small>Leer = nicht belegt. Modul ohne CS = nicht verwendet, ohne GDO0 = nur Empfang/xFSK. LED-Pin ");
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
      EEPROM.commit();
    } else if (a != "bl" && a != "fr" && WS->hasArg("p00")) {
      pinErr = !webSavePins();
    }
#endif
#ifdef CMP_CC1101
    uint8_t b = WS->arg(F("b")).toInt() % EE_BANK_COUNT;
    if (a == "bl") {
      bankLoad(b);
    } else if (a == "fr") {
      cc1101::ccFactoryReset();
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
      delay(500);
      ESP.restart();
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
    p += F("Modul "); p += r + 1; p += F(" (Port "); p += RADIO_PORT_BASE + r; p += ')';
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
  p += F("</td></tr><tr><td>Frequenz MHz</td><td><input name='f' type='number' step='any' min='300' max='928' value='");
  p += String(ccFreq(), 3);
  p += F("'></td></tr><tr><td>bWidth kHz</td><td><select name='bw'>");
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

  p += F("<button name='a' value='set'>&Uuml;bernehmen</button> "
         "<button name='a' value='rb'>Speichern &amp; Neustart</button>");
#ifdef RADIO_PIN_CFG
  webPins(p);
#endif
#ifdef CMP_CC1101
  p += F(" <button name='a' value='fr' onclick=\"return confirm('Werkseinstellungen CC1101?')\">Werkseinstellungen</button>");
#endif
  p += F("</form><hr><a href='/wifi'>WLAN</a> | <a href='/update'>Firmware-Update (OTA)</a> | <a href='/info'>Info</a> | <a href='/custom'>Status</a> | <a href='/param'>Theme</a>"
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

// stop web server after WEB_TIMEOUT_MS uptime, if nobody opened the configuration page
inline void webConfigLoop() {
  if (webPortalOn && !webKeepOpen && !wifiManager.getConfigPortalActive() && millis() > WEB_TIMEOUT_MS) {
    wifiManager.stopWebPortal();
    webPortalOn = false;
    DBG_PRINTLN(F("web server stopped"));
  }
}

#endif
