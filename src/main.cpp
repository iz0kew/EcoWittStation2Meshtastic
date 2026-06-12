// ============================================================================
// main.cpp — Ricevitore meteo Fine Offset/Ecowitt (WH32, WH40, WH57)
//            con bridge telemetria verso la rete Meshtastic
// Schede: Heltec V3 / V4 / Wireless Tracker (ESP32-S3 + SX1262)
//
// La radio e' una sola: resta in ricezione FSK 868.35 MHz e si sposta in LoRa
// solo per i pochi istanti della trasmissione Meshtastic, poi torna in FSK.
// ============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "board_config.h"
#include "user_config.h"
#include "sensors.h"
#include "history.h"
#include "display.h"
#include "meshtastic_tx.h"

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

History  history;
uint32_t g_fskOk = 0, g_fskBad = 0;
uint32_t g_nextMeshMs = 0;

static const uint8_t RX_PKT_LEN = 24;   // copre il frame piu' lungo + margine
static volatile bool rxFlag = false;
static void IRAM_ATTR onRxDone(void) { rxFlag = true; }

// ---------------------------------------------------------------------------
// (Ri)configura l'SX1262 in ricezione FSK per i sensori Fine Offset
// ---------------------------------------------------------------------------
void radioStartFSK() {
  radio.standby();
  int st = radio.beginFSK(RX_FREQ_MHZ, RX_BITRATE_KBPS, RX_FREQ_DEV_KHZ,
                          RX_BW_KHZ, 10, 16);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("beginFSK fallito: %d\n", st);
    return;
  }
  uint8_t syncWord[] = { 0x2D, 0xD4 };
  radio.setSyncWord(syncWord, sizeof(syncWord));
  radio.fixedPacketLengthMode(RX_PKT_LEN);
  radio.setCRC(0);                       // CRC verificato in software
  radio.setRxBoostedGainMode(true);
  radio.setDio2AsRfSwitch(true);
  radio.setDio1Action(onRxDone);
  radio.startReceive();
}

// ---------------------------------------------------------------------------
// Campionatura periodica per grafici e finestre pioggia
// ---------------------------------------------------------------------------
static void sampleHistory() {
  const uint32_t STALE_MS = 15UL * 60UL * 1000UL;  // dato piu' vecchio di 15 min = assente
  uint32_t now = millis();
  HistSample s;
  s.ms      = now;
  s.t10     = (wh32.valid && now - wh32.lastSeen < STALE_MS)
                ? (int16_t)lroundf(wh32.tempC * 10) : INT16_MIN;
  s.rh      = (wh32.valid && now - wh32.lastSeen < STALE_MS)
                ? (int16_t)wh32.humidity : -1;
  s.rain10  = wh40.valid ? (int32_t)lroundf(wh40.rainMm * 10) : -1;
  s.strikes = wh57.valid ? (int32_t)wh57.strikesTotal : -1;
  history.add(s);
}

// ---------------------------------------------------------------------------
// Invio periodico su Meshtastic: media dall'ultimo invio
// ---------------------------------------------------------------------------
#if MESH_ENABLED
static void meshPeriodicSend() {
  bool  haveTH = meshAccum.tN > 0;
  float avgT   = haveTH ? (float)(meshAccum.tSum / meshAccum.tN) : 0;
  float avgH   = haveTH ? (float)(meshAccum.hSum / meshAccum.hN) : 0;

  bool  haveRain = wh40.valid;
  float r1  = haveRain ? history.rainDeltaMm(wh40.rainMm, 3600UL * 1000UL) : 0;
  float r24 = haveRain ? history.rainDeltaMm(wh40.rainMm, 24UL * 3600UL * 1000UL) : 0;

  if (haveTH || haveRain) {
    meshSendTelemetry(haveTH, avgT, avgH, haveRain, r1, r24);
  } else {
    Serial.println("[mesh] nessun dato da inviare, salto il giro");
  }

  meshAccum.reset();
}
#endif

// ---------------------------------------------------------------------------
// Allarme fulmini basato su soglia score = colpi_nella_finestra / distanza_km
// ---------------------------------------------------------------------------
#if MESH_ENABLED && MESH_LIGHTNING_TEXT
namespace {
  struct LightEntry { uint32_t ms; uint32_t strikes; };
  const uint8_t LWIN_SIZE = 16;
  LightEntry    lwin[LWIN_SIZE];
  uint8_t       lwinHead         = 0;
  uint8_t       lwinCount        = 0;
  uint32_t      lwinPrevSeen     = 0;
  uint32_t      lastAlertMs      = 0;
  uint32_t      lastAlertStrikes = 0;
}

static void checkLightningAlert(uint32_t now) {
  if (!wh57.valid || wh57.distanceKm == 63) return;

  // aggiunge un campione ogni nuovo pacchetto WH57
  if (wh57.lastSeen != lwinPrevSeen) {
    lwinPrevSeen = wh57.lastSeen;
    lwin[lwinHead] = { now, wh57.strikesTotal };
    lwinHead = (lwinHead + 1) % LWIN_SIZE;
    if (lwinCount < LWIN_SIZE) lwinCount++;
  }
  if (lwinCount == 0) return;

  // cooldown: un allarme al massimo per finestra temporale
  if (now - lastAlertMs < (uint32_t)MESH_LIGHTNING_WINDOW_MIN * 60000UL) return;

  // cerca il campione più vecchio ancora dentro la finestra
  const uint32_t windowMs = (uint32_t)MESH_LIGHTNING_WINDOW_MIN * 60000UL;
  uint32_t oldestStrikes = wh57.strikesTotal;
  for (uint8_t i = 0; i < lwinCount; i++) {
    uint8_t idx = (lwinHead - 1 - i + LWIN_SIZE) % LWIN_SIZE;
    if (now - lwin[idx].ms <= windowMs)
      oldestStrikes = lwin[idx].strikes;
    else
      break;
  }

  uint32_t delta = (wh57.strikesTotal >= oldestStrikes)
                   ? wh57.strikesTotal - oldestStrikes : 0;
  if (delta == 0) return;

  float score = (float)delta / (float)wh57.distanceKm;
  if (score < MESH_LIGHTNING_THRESHOLD) return;

  // non riallarmare per gli stessi fulmini già segnalati
  if (wh57.strikesTotal <= lastAlertStrikes) return;

  lastAlertMs      = now;
  lastAlertStrikes = wh57.strikesTotal;

  char msg[96];
  snprintf(msg, sizeof(msg), "⚡ " MESH_LONG_NAME "\n%lu fulmini rilevati  ~%u km",
           (unsigned long)delta, (unsigned)wh57.distanceKm);
  meshSendText(msg);
  Serial.printf("[mesh] allarme fulmini: score=%.2f (%lu fulmini @%ukm)\n",
                (double)score, (unsigned long)delta, (unsigned)wh57.distanceKm);
}
#endif

// ---------------------------------------------------------------------------
// Compone e invia un riassunto meteo testuale sul canale
// ---------------------------------------------------------------------------
#if MESH_ENABLED && MESH_TEXT_INTERVAL_MIN > 0
static void meshSendWeatherText() {
  char msg[160];
  size_t n = 0;
  bool hasData = false;

  n += snprintf(msg + n, sizeof(msg) - n, MESH_LONG_NAME "\n");

  if (wh32.valid) {
    n += snprintf(msg + n, sizeof(msg) - n, "🌡️ %.1f°C  💧 %u%%\n",
                 wh32.tempC, wh32.humidity);
    hasData = true;
  }
  if (wh40.valid) {
    float r1  = history.rainDeltaMm(wh40.rainMm, 3600UL * 1000UL);
    float r24 = history.rainDeltaMm(wh40.rainMm, 24UL * 3600UL * 1000UL);
    n += snprintf(msg + n, sizeof(msg) - n, "🌧️ 1h %.1fmm  24h %.1fmm\n", r1, r24);
    hasData = true;
  }
  if (wh57.valid && wh57.strikesTotal > 0) {
    if (wh57.distanceKm != 63)
      n += snprintf(msg + n, sizeof(msg) - n, "⚡ %lu fulmini  ~%u km",
                   (unsigned long)wh57.strikesTotal, wh57.distanceKm);
    else
      n += snprintf(msg + n, sizeof(msg) - n, "⚡ %lu fulmini",
                   (unsigned long)wh57.strikesTotal);
    hasData = true;
  }

  if (hasData)
    meshSendText(msg);
}
#endif

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nEcoWittStation2Meshtastic v%s - %s\n", FW_VERSION, BOARD_NAME);

  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, VEXT_ON_LEVEL);
  delay(50);

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  gfxInit();
  gfxClear();
  gfxText(0, 12, "Init SX1262...");
  gfxFlush();

  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  radioStartFSK();
  Serial.printf("In ascolto FSK su %.2f MHz @ %.2f kbps\n",
                (double)RX_FREQ_MHZ, (double)RX_BITRATE_KBPS);

#if MESH_ENABLED
  meshInit();
  g_nextMeshMs = millis() + (uint32_t)MESH_SEND_INTERVAL_MIN * 60000UL;
#endif

  screensDraw();
}

// ---------------------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // --- ricezione FSK ---
  if (rxFlag) {
    rxFlag = false;
    uint8_t buf[RX_PKT_LEN];
    int st = radio.readData(buf, RX_PKT_LEN);
    float rssi = radio.getRSSI();
    if (st == RADIOLIB_ERR_NONE) {
      Serial.print("[RX] ");
      for (int i = 0; i < RX_PKT_LEN; i++) Serial.printf("%02X ", buf[i]);
      Serial.printf(" (RSSI %.0f dBm)\n", rssi);
      if (decodeSensors(buf, RX_PKT_LEN, rssi)) g_fskOk++;
      else                                      g_fskBad++;
#if MESH_ENABLED && MESH_LIGHTNING_TEXT
      checkLightningAlert(millis());
#endif
    }
    radio.startReceive();
    screensDraw();
  }

  // --- tasto PRG: cambio schermata ---
  static bool btnPrev = HIGH;
  static uint32_t btnLastChange = 0;
  bool btn = digitalRead(PIN_BUTTON);
  if (btn != btnPrev && now - btnLastChange > 60) {
    btnLastChange = now;
    btnPrev = btn;
    if (btn == LOW) {            // pressione
      screensNext();
      screensDraw();
    }
  }

  // --- campionatura storico ---
  static uint32_t nextSample = 0;
  if (now >= nextSample) {
    nextSample = now + (uint32_t)HISTORY_SAMPLE_MIN * 60000UL;
    sampleHistory();
  }

#if MESH_ENABLED
  // --- telemetria periodica ---
  if (g_nextMeshMs && (int32_t)(now - g_nextMeshMs) >= 0) {
    g_nextMeshMs = now + (uint32_t)MESH_SEND_INTERVAL_MIN * 60000UL;
    meshPeriodicSend();
  }

  // --- messaggio testo periodico ---
#if MESH_TEXT_INTERVAL_MIN > 0
  static uint32_t nextTextMs = (uint32_t)MESH_TEXT_INTERVAL_MIN * 60000UL;
  if ((int32_t)(now - nextTextMs) >= 0) {
    nextTextMs = now + (uint32_t)MESH_TEXT_INTERVAL_MIN * 60000UL;
    meshSendWeatherText();
  }
#endif

  // --- NodeInfo + posizione: stesso ciclo di send_interval_min, sfasato di 30s ---
  static uint32_t nextNodeInfo = 0;
  if (nextNodeInfo == 0) nextNodeInfo = g_nextMeshMs + 30000UL;
  if ((int32_t)(now - nextNodeInfo) >= 0) {
    nextNodeInfo += (uint32_t)MESH_SEND_INTERVAL_MIN * 60000UL;
    meshSendNodeInfo();
#if MESH_POS_ENABLED
    delay(1000);
    meshSendPosition();
#endif
  }
#endif

  // --- refresh display ---
  static uint32_t lastDraw = 0;
  if (now - lastDraw > 1000) {
    lastDraw = now;
    screensDraw();
  }
}
