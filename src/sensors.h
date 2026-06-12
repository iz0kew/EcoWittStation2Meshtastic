// ============================================================================
// sensors.h — stato e decodifica dei sensori Fine Offset / Ecowitt
//   WH32 (temp/umidita'), WH40 (pioggia), WH57 (fulmini)
// Formati pacchetto ricavati da rtl_433:
//   fineoffset.c (WH32), ambientweather_wh31e.c (WH40), fineoffset_wh31l.c (WH57)
// ============================================================================
#pragma once
#include <Arduino.h>

struct WH32Data {
  bool     valid    = false;
  uint8_t  id       = 0;
  float    tempC    = 0;
  uint8_t  humidity = 0;
  bool     battLow  = false;
  float    rssi     = 0;
  uint32_t lastSeen = 0;
};

struct WH40Data {
  bool     valid    = false;
  uint16_t id       = 0;
  float    rainMm   = 0;     // contatore cumulativo (passi da 0.1 mm)
  float    battV    = 0;
  float    rssi     = 0;
  uint32_t lastSeen = 0;
};

struct WH57Data {
  bool     valid       = false;
  uint32_t id          = 0;
  uint8_t  state       = 0;   // 0 avvio, 1 interferenza, 4 rumore, 8 fulmine
  uint8_t  distanceKm  = 63;  // 63 = nessun fulmine
  uint32_t strikesTotal = 0;  // accumulato da noi (gestendo il wrap a 8 bit)
  uint8_t  lastCount   = 0;
  bool     haveCount   = false;
  uint8_t  battLevel   = 0;   // 0..3 (0 = scarica)
  float    rssi        = 0;
  uint32_t lastSeen    = 0;
};

// Accumulatore per la media tra un invio Meshtastic e il successivo
struct MeshAccum {
  double   tSum = 0;  uint32_t tN = 0;
  double   hSum = 0;  uint32_t hN = 0;
  uint32_t newStrikes = 0;
  uint8_t  lastStrikeDistKm = 63;
  void reset() { tSum = 0; tN = 0; hSum = 0; hN = 0; newStrikes = 0; lastStrikeDistKm = 63; }
};

extern WH32Data  wh32;
extern WH40Data  wh40;
extern WH57Data  wh57;
extern MeshAccum meshAccum;

// Decodifica un buffer ricevuto via FSK (dopo il sync word 0x2DD4).
// Ritorna true se almeno un sensore e' stato decodificato.
bool decodeSensors(const uint8_t *buf, size_t len, float rssi);
