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
#include "timesync.h"
#include "astro.h"

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

History  history;
uint32_t g_fskOk = 0, g_fskBad = 0;
uint32_t g_nextMeshMs = 0;

static const uint8_t RX_PKT_LEN = 24;   // copre il frame piu' lungo + margine
volatile bool rxFlag = false;            // visibile a timesync.cpp
static void IRAM_ATTR onRxDone(void) { rxFlag = true; }

// ---------------------------------------------------------------------------
// Configura l'SX1262 in ricezione LoRa continua con i parametri Meshtastic.
// Usato da timesync.cpp durante la finestra di sincronizzazione orario.
// ---------------------------------------------------------------------------
void radioStartLoRaRX() {
  radio.standby();
  int st = radio.begin(MESH_FREQ_MHZ, MESH_BW_KHZ, MESH_SF, MESH_CR,
                       0x2B, 10, 16);   // syncWord=0x2B, pwr=10 dBm (RX), preamble=16
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radioStartLoRaRX fallito: %d\n", st);
    return;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setCRC(true);
  radio.setDio1Action(onRxDone);
  radio.startReceive();
}

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

  // cerca il campione piu' vecchio ancora dentro la finestra
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

  // non riallarmare per gli stessi fulmini gia' segnalati
  if (wh57.strikesTotal <= lastAlertStrikes) return;

  lastAlertMs      = now;
  lastAlertStrikes = wh57.strikesTotal;

  char msg[96];
  snprintf(msg, sizeof(msg), "⚡ " MESH_LONG_NAME "\n%lu fulmini  ~%u km",
           (unsigned long)delta, (unsigned)wh57.distanceKm);
  meshSendText(msg);   // canale testo (default chanIdx=1)
  Serial.printf("[mesh] allarme fulmini: score=%.2f (%lu fulmini @%ukm)\n",
                (double)score, (unsigned long)delta, (unsigned)wh57.distanceKm);
}
#endif

// ---------------------------------------------------------------------------
// Compone e invia un bollettino meteo testuale su chanIdx.
// ---------------------------------------------------------------------------
#if MESH_ENABLED
static void meshSendWeatherText(uint8_t chanIdx = 1) {
  char msg[240];
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
      n += snprintf(msg + n, sizeof(msg) - n, "⚡ %lu fulmini  ~%u km\n",
                   (unsigned long)wh57.strikesTotal, wh57.distanceKm);
    else
      n += snprintf(msg + n, sizeof(msg) - n, "⚡ %lu fulmini\n",
                   (unsigned long)wh57.strikesTotal);
    hasData = true;
  }

  // Dati astronomici: inclusi se l'orologio e' sincronizzato.
  if (timeSyncValid()) {
    SunTimes sun;
    if (astroGetSunTimes(sun))
      n += snprintf(msg + n, sizeof(msg) - n, "🌅 %02d:%02d  🌇 %02d:%02d\n",
                   sun.riseH, sun.riseM, sun.setH, sun.setM);
    int illum = (int)(astroMoonIllum() * 100.0f + 0.5f);
    n += snprintf(msg + n, sizeof(msg) - n, "%s %s  %d%%",
                 astroMoonPhaseEmoji(), astroMoonPhaseName(), illum);
    hasData = true;
  }

  n += snprintf(msg + n, sizeof(msg) - n,
               "\n📡 Vuoi la tua stazione meteo sulla mesh? urly.it/31f_d_");
  if (!hasData) return;
  meshSendText(msg, chanIdx);
}

// ---------------------------------------------------------------------------
// Bollettino astronomico: solo canale principale (MediumFast).
// Il canale testo (METEOLAZIO) usa il proprio ciclo text_interval_min.
// ---------------------------------------------------------------------------
static void sendWeatherBulletin() {
  meshSendWeatherText(0);   // canale principale / MediumFast
  Serial.println("[astro] bollettino inviato su ch0 (MediumFast)");
}

// ---------------------------------------------------------------------------
// Scheduler astronomico: alba+1h, mezzogiorno locale, tramonto-1h.
// Calcola i tre orari una volta al giorno; checkAstroSend() va chiamato nel loop().
// ---------------------------------------------------------------------------
namespace {
  struct AstroSchedule {
    time_t  t[3];      // 0=alba+1h, 1=mezzogiorno, 2=tramonto-1h
    bool    sent[3];
    int     lastDay;   // tm_yday dell'ultimo calcolo (-1 = mai)
  };
  AstroSchedule g_asch = { {0, 0, 0}, {false, false, false}, -1 };
}

static void updateAstroSchedule(const struct tm &lt) {
  if (lt.tm_yday == g_asch.lastDay) return;  // gia' calcolato oggi
  g_asch.lastDay    = lt.tm_yday;
  g_asch.sent[0] = g_asch.sent[1] = g_asch.sent[2] = false;

  SunTimes sun;
  if (!astroGetSunTimes(sun)) {
    g_asch.t[0] = g_asch.t[1] = g_asch.t[2] = 0;
    return;
  }

  // mezzanotte locale del giorno corrente
  struct tm base = lt;
  base.tm_hour = 0; base.tm_min = 0; base.tm_sec = 0;
  time_t midnight = mktime(&base);

  g_asch.t[0] = midnight + (time_t)(sun.riseH * 3600 + sun.riseM * 60) + 3600; // alba+1h
  g_asch.t[1] = midnight + 12 * 3600;                                            // mezzogiorno
  g_asch.t[2] = midnight + (time_t)(sun.setH  * 3600 + sun.setM  * 60) - 3600; // tramonto-1h

  Serial.printf("[astro] schedule oggi: slot0=%02d:%02d  slot1=12:00  slot2=%02d:%02d\n",
                (int)((g_asch.t[0] - midnight) / 3600),
                (int)(((g_asch.t[0] - midnight) % 3600) / 60),
                (int)((g_asch.t[2] - midnight) / 3600),
                (int)(((g_asch.t[2] - midnight) % 3600) / 60));

  // Pre-marca gli slot già passati come inviati per evitare bollettini
  // spuri al riavvio quando la sync avviene dopo uno degli orari previsti.
  time_t nowT = time(nullptr);
  for (int i = 0; i < 3; i++)
    if (g_asch.t[i] > 0 && nowT >= g_asch.t[i]) g_asch.sent[i] = true;
}

static void checkAstroSend() {
  if (!timeSyncValid()) return;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);

  updateAstroSchedule(lt);

  for (int i = 0; i < 3; i++) {
    if (!g_asch.sent[i] && g_asch.t[i] > 0 && now >= g_asch.t[i]) {
      g_asch.sent[i] = true;
      sendWeatherBulletin();
      break;   // un invio per ciclo di loop per evitare TX ravvicinati
    }
  }
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

#if MESH_ENABLED
  meshInit();
  // Il primo TX e' posticipato di tutta la finestra di sync + 30 s di margine,
  // cosi' la radio e' libera di ricevere durante la sincronizzazione.
  g_nextMeshMs = millis() + TSYNC_WINDOW_MS + 30000UL;
#endif

  // Avvia la sincronizzazione orario: porta la radio in LoRa RX per 5 min.
  timeSyncBegin();
  screensSet(SCR_TIME);
  screensDraw();
  Serial.println("Finestra time-sync aperta (5 min) -- premi PRG per navigare");
}

// ---------------------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // =========================================================================
  // FASE DI SINCRONIZZAZIONE ORARIO (primi 5 min dall'avvio)
  // La radio e' in LoRa RX; i pacchetti Meshtastic vengono passati a timesync.
  // =========================================================================
  if (!timeSyncDone()) {
    timeSyncTick();   // gestisce rxFlag e timeout internamente

    // tasto PRG: l'utente puo' gia' navigare tra le schermate
    static bool btnPrevSync = HIGH;
    static uint32_t btnLastSync = 0;
    bool btn = digitalRead(PIN_BUTTON);
    if (btn != btnPrevSync && now - btnLastSync > 60) {
      btnLastSync = now;
      btnPrevSync = btn;
      if (btn == LOW) { screensNext(); screensDraw(); }
    }

    // refresh ogni secondo per aggiornare il conto alla rovescia
    static uint32_t lastSyncDraw = 0;
    if (now - lastSyncDraw > 1000) {
      lastSyncDraw = now;
      screensDraw();
    }
    return;   // salta il resto del loop finche' la sync e' in corso
  }

  // =========================================================================
  // Avvio normale FSK (eseguito una sola volta, alla prima uscita dalla sync)
  // =========================================================================
  static bool fskStarted = false;
  if (!fskStarted) {
    fskStarted = true;
    radioStartFSK();
    Serial.printf("In ascolto FSK su %.2f MHz @ %.2f kbps\n",
                  (double)RX_FREQ_MHZ, (double)RX_BITRATE_KBPS);
  }

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
    if (btn == LOW) {
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

  // --- bollettino meteo ad orari astronomici su ch0 (MediumFast) ---
  checkAstroSend();

  // --- bollettino meteo a intervallo fisso su ch1 (METEOLAZIO) ---
#if MESH_TEXT_INTERVAL_MIN > 0
  static uint32_t nextTextMs = (uint32_t)MESH_TEXT_INTERVAL_MIN * 60000UL;
  if ((int32_t)(now - nextTextMs) >= 0) {
    nextTextMs = now + (uint32_t)MESH_TEXT_INTERVAL_MIN * 60000UL;
    meshSendWeatherText(1);   // canale testo / METEOLAZIO
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
