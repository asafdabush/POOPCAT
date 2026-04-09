/*******************************************************
 * POOPCAT - ESP8266 + TC4427 Differential Drive + RCWL + Button + Blynk + WiFi Failover
 *
 * Blynk:
 *   V0 = Remote Enable (0/1)
 *   V1 = Fixed frequency slider (0..35000 Hz)
 *   V2 = Motion LED (0/1)  [SAFE: change-only + max every 5s]
 *   V3 = Speaker LED (0/1) [SAFE: change-only + max every 5s]
 *
 * Key behavior:
 * - Start DISABLED
 * - Button toggles system:
 *     ON  -> LED blinks 1x
 *     OFF -> LED blinks 3x
 * - While system enabled:
 *     ST_LISTEN: LED reflects motion directly
 *     ST_ACTIVE: Ultrasound ON, ignore motion, LED forced LOW
 *     ST_COOLDOWN: ultrasound OFF, ignore motion for COOLDOWN_MS
 * WiFi: Two SSIDs via ESP8266WiFiMulti (auto fallback)
 *******************************************************/

#define BLYNK_PRINT Serial
// Copy secrets_template.h -> secrets.h and fill in your values
#include "secrets.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <BlynkSimpleEsp8266.h>
#include <ets_sys.h>

ESP8266WiFiMulti wifiMulti;

static const uint8_t PIN_US_A   = 5;   // D1
static const uint8_t PIN_US_B   = 4;   // D2 (inverted)
static const uint8_t PIN_MOTION = 14;  // D5
static const uint8_t PIN_BTN    = 12;  // D6
static const uint8_t PIN_LED    = 13;  // D7

static volatile uint32_t FIXED_FREQ_HZ = 25000;
static const uint32_t MOTION_HOLD_MS  = 1000;
static const uint32_t COOLDOWN_MS     = 500;
static const uint32_t ACTIVE_TOTAL_MS = 12000;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static const uint16_t BLINK_ON_MS     = 120;
static const uint16_t BLINK_OFF_MS    = 120;
static const uint32_t STATUS_MIN_PERIOD_MS = 5000;

static int lastV2 = -1, lastV3 = -1;
static uint32_t lastStatusSendMs = 0;

enum State : uint8_t { ST_LISTEN, ST_ACTIVE, ST_COOLDOWN };
static State state = ST_LISTEN;
static bool systemEnabled = false;
static uint32_t motionHighStartMs = 0, stateStartMs = 0, activeStartMs = 0;
static volatile bool usEnabled = false, usToggleState = false;
static uint32_t lastBlynkTryMs = 0;
static const uint32_t BLYNK_RETRY_MS = 7000;

void ICACHE_RAM_ATTR onTimer1() {
  if (!usEnabled) { digitalWrite(PIN_US_A, LOW); digitalWrite(PIN_US_B, LOW); return; }
  usToggleState = !usToggleState;
  digitalWrite(PIN_US_A, usToggleState ? HIGH : LOW);
  digitalWrite(PIN_US_B, usToggleState ? LOW  : HIGH);
}

static void setFrequencyHz(uint32_t freqHz) {
  if (freqHz < 1000)  freqHz = 1000;
  if (freqHz > 35000) freqHz = 35000;
  uint32_t cycles = 5000000UL / (freqHz * 2UL);
  if (cycles < 50) cycles = 50;
  if (cycles > 0x7FFFFF) cycles = 0x7FFFFF;
  timer1_disable();
  timer1_attachInterrupt(onTimer1);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(cycles);
}

static inline void ultrasoundOn()  { usEnabled = true; }
static inline void ultrasoundOff() {
  usEnabled = false;
  digitalWrite(PIN_US_A, LOW);
  digitalWrite(PIN_US_B, LOW);
}
static uint32_t clampFreq(uint32_t f) {
  if (f < 1000) f = 1000; if (f > 35000) f = 35000; return f;
}

static void pumpWait(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    wifiMulti.run();
    if (WiFi.status() == WL_CONNECTED && !Blynk.connected() &&
        (millis() - lastBlynkTryMs) > BLYNK_RETRY_MS) {
      lastBlynkTryMs = millis(); Blynk.connect(1500);
    }
    Blynk.run(); delay(1);
  }
}

static void blinkLed(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(PIN_LED, HIGH); pumpWait(BLINK_ON_MS);
    digitalWrite(PIN_LED, LOW);  pumpWait(BLINK_OFF_MS);
  }
}

static void safeSendStatus(int v2, int v3) {
  if (!Blynk.connected() || (v2 == lastV2 && v3 == lastV3)) return;
  uint32_t now = millis();
  if (now - lastStatusSendMs < STATUS_MIN_PERIOD_MS) return;
  lastStatusSendMs = now; lastV2 = v2; lastV3 = v3;
  Blynk.virtualWrite(V2, v2); Blynk.virtualWrite(V3, v3);
}

static bool btnStable = HIGH, btnLastRead = HIGH;
static uint32_t btnLastChangeMs = 0;

static void setSystemEnabled(bool en) {
  if (en == systemEnabled) return;
  if (en) {
    ultrasoundOff(); digitalWrite(PIN_LED, LOW); blinkLed(1);
    systemEnabled = true; state = ST_LISTEN; motionHighStartMs = 0;
  } else {
    systemEnabled = false; ultrasoundOff(); digitalWrite(PIN_LED, LOW);
    blinkLed(3); state = ST_LISTEN; motionHighStartMs = 0; digitalWrite(PIN_LED, LOW);
  }
}

static void handleButton() {
  bool r = digitalRead(PIN_BTN); uint32_t now = millis();
  if (r != btnLastRead) { btnLastRead = r; btnLastChangeMs = now; }
  if ((now - btnLastChangeMs) >= BTN_DEBOUNCE_MS && btnStable != r) {
    btnStable = r;
    if (btnStable == LOW) setSystemEnabled(!systemEnabled);
  }
}

BLYNK_WRITE(V0) { setSystemEnabled(param.asInt() != 0); }
BLYNK_WRITE(V1) {
  int f = param.asInt();
  if (f <= 0) f = 25000; if (f > 35000) f = 35000;
  FIXED_FREQ_HZ = clampFreq((uint32_t)f);
  if (systemEnabled && state == ST_ACTIVE && usEnabled) setFrequencyHz(FIXED_FREQ_HZ);
}
BLYNK_CONNECTED() {}

static void setupWiFiMulti() {
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.persistent(false);
  wifiMulti.addAP(WIFI1_SSID, WIFI1_PASS);
  wifiMulti.addAP(WIFI2_SSID, WIFI2_PASS);
}

static void tryConnectBlynkNonBlocking() {
  wifiMulti.run();
  if (WiFi.status() != WL_CONNECTED) return;
  if (!Blynk.connected()) {
    uint32_t now = millis();
    if (now - lastBlynkTryMs >= BLYNK_RETRY_MS) { lastBlynkTryMs = now; Blynk.connect(1500); }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_US_A, OUTPUT); pinMode(PIN_US_B, OUTPUT);
  pinMode(PIN_LED, OUTPUT);  pinMode(PIN_MOTION, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  digitalWrite(PIN_LED, LOW); ultrasoundOff();
  setFrequencyHz(FIXED_FREQ_HZ);
  systemEnabled = false; state = ST_LISTEN; motionHighStartMs = 0;
  setupWiFiMulti();
  Blynk.config(BLYNK_AUTH_TOKEN);
  lastV2 = -1; lastV3 = -1; lastStatusSendMs = 0; lastBlynkTryMs = 0;
}

void loop() {
  tryConnectBlynkNonBlocking(); Blynk.run(); handleButton();
  if (!systemEnabled) { ultrasoundOff(); digitalWrite(PIN_LED, LOW); safeSendStatus(0,0); return; }
  uint32_t now = millis();
  bool motionRaw = (digitalRead(PIN_MOTION) == HIGH);
  switch (state) {
    case ST_LISTEN:
      digitalWrite(PIN_LED, motionRaw ? HIGH : LOW);
      safeSendStatus(motionRaw ? 1 : 0, 0);
      if (motionRaw) {
        if (motionHighStartMs == 0) motionHighStartMs = now;
        if ((now - motionHighStartMs) >= MOTION_HOLD_MS) {
          state = ST_ACTIVE; activeStartMs = stateStartMs = now; motionHighStartMs = 0;
          setFrequencyHz(FIXED_FREQ_HZ); ultrasoundOn(); safeSendStatus(0, 1);
        }
      } else { motionHighStartMs = 0; }
      break;
    case ST_ACTIVE:
      digitalWrite(PIN_LED, LOW);
      setFrequencyHz(FIXED_FREQ_HZ); ultrasoundOn(); safeSendStatus(0, 1);
      if ((now - activeStartMs) >= ACTIVE_TOTAL_MS) {
        ultrasoundOff(); state = ST_COOLDOWN; stateStartMs = now; safeSendStatus(0, 0);
      }
      break;
    case ST_COOLDOWN:
      ultrasoundOff(); digitalWrite(PIN_LED, LOW); safeSendStatus(0, 0);
      if ((now - stateStartMs) >= COOLDOWN_MS) { state = ST_LISTEN; motionHighStartMs = 0; }
      break;
  }
}
