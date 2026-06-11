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

#if MESH_LIGHTNING_TEXT
  if (meshAccum.newStrikes > 0) {
    char msg[64];
    if (meshAccum.lastStrikeDistKm != 63)
      snprintf(msg, sizeof(msg), "Fulmini: %lu (ultimo ~%u km)",
               (unsigned long)meshAccum.newStrikes, meshAccum.lastStrikeDistKm);
    else
      snprintf(msg, sizeof(msg), "Fulmini: %lu",
               (unsigned long)meshAccum.newStrikes);
    delay(1000);                 // piccolo distacco tra i due pacchetti
    meshSendText(msg);
  }
#endif

  meshAccum.reset();
}
#endif

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nRicevitore meteo - %s\n", BOARD_NAME);

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

  // --- NodeInfo + posizione: al primo avvio (dopo 1 min) e poi ogni ora ---
  static uint32_t nextNodeInfo = 60000UL;
  if ((int32_t)(now - nextNodeInfo) >= 0) {
    nextNodeInfo = now + 3600000UL;
    meshSendNodeInfo();
#if MESH_POS_ENABLED
    delay(1000);                 // piccolo distacco tra i due pacchetti
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
