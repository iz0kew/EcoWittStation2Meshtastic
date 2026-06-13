#include "sensors.h"

WH32Data  wh32;
WH40Data  wh40;
WH57Data  wh57;
MeshAccum meshAccum;

// CRC-8, polinomio 0x31, init 0x00, MSB-first (standard Fine Offset)
static uint8_t crc8_0x31(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

static uint8_t sum8(const uint8_t *data, size_t len) {
  uint8_t s = 0;
  for (size_t i = 0; i < len; i++) s += data[i];
  return s;
}

// ---------------------------------------------------------------------------
// WH32 (e WH25/WH32B): 8 byte  MI IT TT HH PP PP CC XX
// ---------------------------------------------------------------------------
static bool tryWH32(const uint8_t *b, float rssi) {
  uint8_t msgType = b[0] & 0xF0;
  if (msgType != 0xD0 && msgType != 0xE0) return false;
  if (sum8(b, 6) != b[6]) return false;

  uint8_t  id      = (uint8_t)(((b[0] & 0x0F) << 4) | (b[1] >> 4));
  bool     battLow = (b[1] & 0x08) != 0;
  bool     invalid = (b[1] & 0x04) != 0;
  uint16_t rawT    = (uint16_t)(((b[1] & 0x03) << 8) | b[2]);
  float    tempC   = (rawT - 400) * 0.1f;
  uint8_t  hum     = b[3];

  if (invalid || rawT == 0x7FF) return false;
  if (hum > 100 || tempC < -45.0f || tempC > 70.0f) return false;

  wh32.valid = true;   wh32.id = id;
  wh32.tempC = tempC;  wh32.humidity = hum;
  wh32.battLow = battLow;
  wh32.rssi = rssi;    wh32.lastSeen = millis();

  meshAccum.tSum += tempC; meshAccum.tN++;
  meshAccum.hSum += hum;   meshAccum.hN++;

  Serial.printf("[WH32] id=%u T=%.1fC RH=%u%% batt=%s RSSI=%.0fdBm\n",
                id, tempC, hum, battLow ? "LOW" : "ok", rssi);
  return true;
}

// ---------------------------------------------------------------------------
// WH40 pluviometro: 9 byte  40 00 II II FV RR RR CC SS
// CRC sui primi 8 byte (incluso il byte CRC -> risultato 0), SUM su 8 byte
// ---------------------------------------------------------------------------
static bool tryWH40(const uint8_t *b, float rssi) {
  if (b[0] != 0x40) return false;
  if (crc8_0x31(b, 8) != 0) return false;
  if (sum8(b, 8) != b[8]) return false;

  uint16_t id      = (uint16_t)((b[2] << 8) | b[3]);
  uint8_t  battRaw = b[4] & 0x1F;            // tensione in passi da 0.1 V
  uint16_t rainRaw = (uint16_t)((b[5] << 8) | b[6]);

  wh40.valid  = true;  wh40.id = id;
  wh40.rainMm = rainRaw * 0.1f;
  wh40.battV  = battRaw * 0.1f;
  wh40.rssi   = rssi;  wh40.lastSeen = millis();

  Serial.printf("[WH40] id=%u pioggia=%.1fmm batt=%.1fV RSSI=%.0fdBm\n",
                id, wh40.rainMm, wh40.battV, rssi);
  return true;
}

// ---------------------------------------------------------------------------
// WH57 fulmini: 9 byte  57 SI II II FF KK CC XX AA
// state = b[1]>>4 (8 = fulmine), distanza = b[5]&0x3F, conteggio = b[6]
// ---------------------------------------------------------------------------
static bool tryWH57(const uint8_t *b, float rssi) {
  if (b[0] != 0x57) return false;
  if (crc8_0x31(b, 8) != 0) return false;
  if (sum8(b, 8) != b[8]) return false;

  uint8_t state = b[1] >> 4;
  uint8_t dist  = b[5] & 0x3F;
  uint8_t count = b[6];

  wh57.valid = true;
  wh57.id    = ((uint32_t)(b[1] & 0x0F) << 16) | ((uint32_t)b[2] << 8) | b[3];
  wh57.state = state;
  wh57.battLevel = (b[4] >> 1) & 0x03;
  wh57.rssi  = rssi;
  wh57.lastSeen = millis();

  if (state == 0) {                 // riavvio/cambio batterie: riparte il contatore
    wh57.haveCount = false;
  }
  if (wh57.haveCount) {
    uint8_t delta = (uint8_t)(count - wh57.lastCount);  // gestisce il wrap a 8 bit
    if (delta > 0 && delta < 128) {                     // scarta valori assurdi
      wh57.strikesTotal     += delta;
      meshAccum.newStrikes  += delta;
      meshAccum.lastStrikeDistKm = dist;
    }
  }
  wh57.lastCount = count;
  wh57.haveCount = true;
  if (state == 8 && dist != 63) wh57.distanceKm = dist;

  Serial.printf("[WH57] id=%06X stato=%u dist=%ukm count=%u tot=%lu batt=%u RSSI=%.0fdBm\n",
                (unsigned)wh57.id, state, dist, count,
                (unsigned long)wh57.strikesTotal, wh57.battLevel, rssi);
  return true;
}

// ---------------------------------------------------------------------------
bool decodeSensors(const uint8_t *buf, size_t len, float rssi) {
  // il frame dovrebbe partire a offset 0, ma scandiamo i primi byte
  // per tollerare disallineamenti del sync
  for (size_t off = 0; off + 9 <= len && off <= 4; off++) {
    const uint8_t *b = buf + off;
    switch (b[0] & 0xF0) {
      case 0xD0: case 0xE0: if (tryWH32(b, rssi)) return true; break;
      case 0x40:            if (tryWH40(b, rssi)) return true; break;
      case 0x50:            if (b[0] == 0x57 && tryWH57(b, rssi)) return true; break;
      default: break;
    }
  }
  return false;
}
