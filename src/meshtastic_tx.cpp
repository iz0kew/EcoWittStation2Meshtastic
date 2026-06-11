#include "meshtastic_tx.h"
#include "user_config.h"
#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <CTR.h>
#include <esp_system.h>
#include <esp_mac.h>

// radio condivisa e ripristino FSK, definiti in main.cpp
extern SX1262 radio;
void radioStartFSK();

// ---------------------------------------------------------------------------
// Costanti protocollo Meshtastic
// ---------------------------------------------------------------------------
// PSK di default dei canali pubblici (AES128) — da Channels.h
static const uint8_t DEFAULT_PSK[16] = {
  0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
  0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};
static const uint32_t BROADCAST_ADDR = 0xFFFFFFFF;
static const uint8_t  LORA_SYNCWORD  = 0x2B;
static const uint16_t LORA_PREAMBLE  = 16;
// portnum (portnums.proto)
static const uint8_t PORT_TEXT      = 1;
static const uint8_t PORT_POSITION  = 3;
static const uint8_t PORT_NODEINFO  = 4;
static const uint8_t PORT_TELEMETRY = 67;

static uint32_t s_nodeId   = 0;
static uint32_t s_pktSent  = 0;
static uint8_t  s_chanHash = 0;

// ---------------------------------------------------------------------------
// Mini-encoder protobuf (solo cio' che serve)
// ---------------------------------------------------------------------------
static size_t pbVarint(uint8_t *buf, size_t p, uint64_t v) {
  while (v >= 0x80) { buf[p++] = (uint8_t)(v | 0x80); v >>= 7; }
  buf[p++] = (uint8_t)v;
  return p;
}
static size_t pbTag(uint8_t *buf, size_t p, uint32_t field, uint8_t wire) {
  return pbVarint(buf, p, ((uint64_t)field << 3) | wire);
}
static size_t pbVarintField(uint8_t *buf, size_t p, uint32_t field, uint64_t v) {
  p = pbTag(buf, p, field, 0);
  return pbVarint(buf, p, v);
}
static size_t pbFloatField(uint8_t *buf, size_t p, uint32_t field, float f) {
  p = pbTag(buf, p, field, 5);
  memcpy(buf + p, &f, 4);          // little-endian su ESP32
  return p + 4;
}
static size_t pbFixed32Field(uint8_t *buf, size_t p, uint32_t field, uint32_t v) {
  p = pbTag(buf, p, field, 5);
  memcpy(buf + p, &v, 4);
  return p + 4;
}
static size_t pbBytesField(uint8_t *buf, size_t p, uint32_t field,
                           const uint8_t *data, size_t len) {
  p = pbTag(buf, p, field, 2);
  p = pbVarint(buf, p, len);
  memcpy(buf + p, data, len);
  return p + len;
}

// ---------------------------------------------------------------------------
// Hash canale: xorHash(nome) ^ xorHash(psk) — da Channels.cpp
// ---------------------------------------------------------------------------
static uint8_t xorHash(const uint8_t *p, size_t len) {
  uint8_t code = 0;
  for (size_t i = 0; i < len; i++) code ^= p[i];
  return code;
}

// ---------------------------------------------------------------------------
void meshInit() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  // come Meshtastic: node number = ultimi 4 byte del MAC
  s_nodeId = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
             ((uint32_t)mac[4] << 8)  |  (uint32_t)mac[5];
  if (s_nodeId == 0 || s_nodeId == BROADCAST_ADDR) s_nodeId = 0x12345678;

  s_chanHash = xorHash((const uint8_t *)MESH_CHANNEL_NAME, strlen(MESH_CHANNEL_NAME))
             ^ xorHash(DEFAULT_PSK, sizeof(DEFAULT_PSK));

  Serial.printf("[mesh] nodo !%08lx canale '%s' hash 0x%02x freq %.3f SF%d BW%g CR4/%d\n",
                (unsigned long)s_nodeId, MESH_CHANNEL_NAME, s_chanHash,
                (double)MESH_FREQ_MHZ, MESH_SF, (double)MESH_BW_KHZ, MESH_CR);
}

uint32_t meshNodeId()      { return s_nodeId; }
uint32_t meshPacketsSent() { return s_pktSent; }

// ---------------------------------------------------------------------------
// Cifra il payload (AES128-CTR) e trasmette header+payload in LoRa,
// poi ripristina la ricezione FSK. Da CryptoEngine.cpp:
//   nonce[0..7] = packetId (LE, esteso a 64 bit), nonce[8..11] = fromNode (LE)
// ---------------------------------------------------------------------------
static bool meshTransmit(const uint8_t *plain, size_t plainLen) {
  if (plainLen > 200) return false;

  uint32_t pktId = esp_random();
  if (pktId == 0) pktId = 1;

  // --- cifratura ---
  uint8_t nonce[16] = {0};
  uint64_t id64 = pktId;
  memcpy(nonce, &id64, 8);
  memcpy(nonce + 8, &s_nodeId, 4);

  uint8_t cipher[208];
  CTR<AES128> ctr;
  ctr.setKey(DEFAULT_PSK, 16);
  ctr.setIV(nonce, 16);
  ctr.encrypt(cipher, plain, plainLen);

  // --- header 16 byte (RadioInterface.h) ---
  uint8_t pkt[224];
  uint32_t dest = BROADCAST_ADDR;
  memcpy(pkt + 0,  &dest,     4);
  memcpy(pkt + 4,  &s_nodeId, 4);
  memcpy(pkt + 8,  &pktId,    4);
  const uint8_t hopLimit = MESH_HOP_LIMIT, hopStart = MESH_HOP_LIMIT;
  pkt[12] = (hopLimit & 0x07) | (uint8_t)(hopStart << 5);   // flags
  pkt[13] = s_chanHash;                                     // channel hash
  pkt[14] = 0;                                              // next_hop
  pkt[15] = (uint8_t)(s_nodeId & 0xFF);                     // relay_node
  memcpy(pkt + 16, cipher, plainLen);

  // --- passa in LoRa, trasmetti, torna in FSK ---
  radio.standby();
  int st = radio.begin(MESH_FREQ_MHZ, MESH_BW_KHZ, MESH_SF, MESH_CR,
                       LORA_SYNCWORD, MESH_TX_POWER_DBM, LORA_PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[mesh] begin LoRa fallito: %d\n", st);
    radioStartFSK();
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setCRC(true);

  st = radio.transmit(pkt, 16 + plainLen);
  radioStartFSK();

  if (st == RADIOLIB_ERR_NONE) {
    s_pktSent++;
    Serial.printf("[mesh] TX ok, id=0x%08lx, %u byte\n",
                  (unsigned long)pktId, (unsigned)(16 + plainLen));
    return true;
  }
  Serial.printf("[mesh] TX fallita: %d\n", st);
  return false;
}

// ---------------------------------------------------------------------------
// Data { portnum=1 varint; payload=2 bytes } -> cifrato e trasmesso
// ---------------------------------------------------------------------------
static bool sendData(uint8_t portnum, const uint8_t *payload, size_t len) {
  uint8_t data[200];
  size_t p = 0;
  p = pbVarintField(data, p, 1, portnum);
  p = pbBytesField(data, p, 2, payload, len);
  return meshTransmit(data, p);
}

// ---------------------------------------------------------------------------
bool meshSendTelemetry(bool haveTH, float tempC, float rh,
                       bool haveRain, float rain1h, float rain24h) {
  if (!haveTH && !haveRain) return false;

  // EnvironmentMetrics (telemetry.proto): temperature=1, relative_humidity=2,
  // rainfall_1h=19, rainfall_24h=20 (tutti float)
  uint8_t env[64];
  size_t e = 0;
  if (haveTH) {
    e = pbFloatField(env, e, 1, tempC);
    e = pbFloatField(env, e, 2, rh);
  }
  if (haveRain) {
    e = pbFloatField(env, e, 19, rain1h);
    e = pbFloatField(env, e, 20, rain24h);
  }

  // Telemetry { fixed32 time=1; environment_metrics=3 }
  uint8_t tel[80];
  size_t t = 0;
  t = pbFixed32Field(tel, t, 1, 0);            // time sconosciuto
  t = pbBytesField(tel, t, 3, env, e);

  return sendData(PORT_TELEMETRY, tel, t);
}

// ---------------------------------------------------------------------------
bool meshSendNodeInfo() {
  // User { id=1 str; long_name=2 str; short_name=3 str; hw_model=5 enum }
  char idStr[12];
  snprintf(idStr, sizeof(idStr), "!%08lx", (unsigned long)s_nodeId);

  uint8_t user[96];
  size_t u = 0;
  u = pbBytesField(user, u, 1, (const uint8_t *)idStr, strlen(idStr));
  u = pbBytesField(user, u, 2, (const uint8_t *)MESH_LONG_NAME,
                   strlen(MESH_LONG_NAME));
  u = pbBytesField(user, u, 3, (const uint8_t *)MESH_SHORT_NAME,
                   strlen(MESH_SHORT_NAME));
  u = pbVarintField(user, u, 5, 255);          // hw_model = PRIVATE_HW

  return sendData(PORT_NODEINFO, user, u);
}

// ---------------------------------------------------------------------------
bool meshSendText(const char *txt) {
  return sendData(PORT_TEXT, (const uint8_t *)txt, strlen(txt));
}

// ---------------------------------------------------------------------------
// Posizione fissa della stazione (mesh.proto, message Position):
//   latitude_i=1 sfixed32 (gradi*1e7), longitude_i=2 sfixed32,
//   altitude=3 int32 (m s.l.m.), time=4 fixed32,
//   location_source=5 (1=LOC_MANUAL), altitude_source=6 (1=ALT_MANUAL),
//   precision_bits=23 (32 = precisione piena)
// ---------------------------------------------------------------------------
bool meshSendPosition() {
#if MESH_POS_ENABLED
  uint8_t pos[64];
  size_t p = 0;
  p = pbFixed32Field(pos, p, 1, (uint32_t)(int32_t)MESH_LAT_I);
  p = pbFixed32Field(pos, p, 2, (uint32_t)(int32_t)MESH_LON_I);
  // int32 negativo -> varint del valore esteso in segno a 64 bit
  p = pbTag(pos, p, 3, 0);
  p = pbVarint(pos, p, (uint64_t)(int64_t)(int32_t)MESH_ALT_M);
  p = pbFixed32Field(pos, p, 4, 0);          // time sconosciuto
  p = pbVarintField(pos, p, 5, 1);           // LOC_MANUAL
  p = pbVarintField(pos, p, 6, 1);           // ALT_MANUAL
  p = pbVarintField(pos, p, 23, 32);         // precisione piena
  return sendData(PORT_POSITION, pos, p);
#else
  return false;
#endif
}
