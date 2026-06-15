#include "meshtastic_tx.h"
#include "timesync.h"
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
// PSK dei canali — configurati in settings.ini -> user_config.h
// Canale 0: principale (telemetria, NodeInfo, posizione)
// Canale 1: testo/fulmini (uguale al principale se MESH_TEXT_CHANNEL_ENABLED==0)
// MESH_CHANNEL_KEY_SIZE e MESH_TEXT_CHANNEL_KEY_SIZE sono 16 (AES-128) o 32 (AES-256)
static const uint8_t s_psk0[MESH_CHANNEL_KEY_SIZE]      = MESH_CHANNEL_KEY;
static const uint8_t s_psk1[MESH_TEXT_CHANNEL_KEY_SIZE] = MESH_TEXT_CHANNEL_KEY;

static const uint32_t BROADCAST_ADDR = 0xFFFFFFFF;
static const uint8_t  LORA_SYNCWORD  = 0x2B;
static const uint16_t LORA_PREAMBLE  = 16;
// portnum (portnums.proto)
static const uint8_t PORT_TEXT      = 1;
static const uint8_t PORT_POSITION  = 3;
static const uint8_t PORT_NODEINFO  = 4;
static const uint8_t PORT_TELEMETRY = 67;

static uint32_t s_nodeId    = 0;
static uint32_t s_pktSent   = 0;
static uint8_t  s_chanHash0 = 0;                          // canale principale
static uint8_t  s_chanHash1 = 0;                          // canale testo/fulmini
static const size_t s_pskLen0 = MESH_CHANNEL_KEY_SIZE;
static const size_t s_pskLen1 = MESH_TEXT_CHANNEL_KEY_SIZE;
static char     s_shortName[5] = MESH_SHORT_NAME;

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
// Cifra con AES-128 o AES-256 in modalita' CTR a seconda della lunghezza della chiave.
// key deve essere 16 o 32 byte; iv deve essere 16 byte.
// ---------------------------------------------------------------------------
static void aesEncrypt(uint8_t *out, const uint8_t *in, size_t len,
                       const uint8_t *key, size_t keyLen, const uint8_t *iv) {
  if (keyLen == 32) {
    CTR<AES256> ctr;
    ctr.setKey(key, 32);
    ctr.setIV(iv, 16);
    ctr.encrypt(out, in, len);
  } else {
    CTR<AES128> ctr;
    ctr.setKey(key, 16);
    ctr.setIV(iv, 16);
    ctr.encrypt(out, in, len);
  }
}

// ---------------------------------------------------------------------------
void meshInit() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  // come Meshtastic: node number = ultimi 4 byte del MAC
  s_nodeId = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
             ((uint32_t)mac[4] << 8)  |  (uint32_t)mac[5];
  if (s_nodeId == 0 || s_nodeId == BROADCAST_ADDR) s_nodeId = 0x12345678;

#if MESH_SHORT_NAME_AUTO
  snprintf(s_shortName, sizeof(s_shortName), "%04lX", (unsigned long)(s_nodeId & 0xFFFF));
#endif

  s_chanHash0 = xorHash((const uint8_t *)MESH_CHANNEL_NAME, strlen(MESH_CHANNEL_NAME))
              ^ xorHash(s_psk0, sizeof(s_psk0));
  s_chanHash1 = xorHash((const uint8_t *)MESH_TEXT_CHANNEL_NAME, strlen(MESH_TEXT_CHANNEL_NAME))
              ^ xorHash(s_psk1, sizeof(s_psk1));

  Serial.printf("[mesh] nodo !%08lx  freq %.3f SF%d BW%g CR4/%d\n",
                (unsigned long)s_nodeId,
                (double)MESH_FREQ_MHZ, MESH_SF, (double)MESH_BW_KHZ, MESH_CR);
  Serial.printf("[mesh] canale principale '%s' hash 0x%02x\n",
                MESH_CHANNEL_NAME, s_chanHash0);
#if MESH_TEXT_CHANNEL_ENABLED
  Serial.printf("[mesh] canale testo      '%s' hash 0x%02x\n",
                MESH_TEXT_CHANNEL_NAME, s_chanHash1);
#else
  Serial.printf("[mesh] canale testo: stesso del principale\n");
#endif
}

uint32_t meshNodeId()      { return s_nodeId; }
uint32_t meshPacketsSent() { return s_pktSent; }

// ---------------------------------------------------------------------------
// Cifra il payload (AES128-CTR) e trasmette header+payload in LoRa,
// poi ripristina la ricezione FSK. Da CryptoEngine.cpp:
//   nonce[0..7] = packetId (LE, esteso a 64 bit), nonce[8..11] = fromNode (LE)
// chanIdx: 0 = canale principale, 1 = canale testo/fulmini
// ---------------------------------------------------------------------------
static bool meshTransmit(const uint8_t *plain, size_t plainLen, uint8_t chanIdx = 0) {
  if (plainLen > 200) return false;

  // seleziona PSK, lunghezza e channel hash in base al canale richiesto
  const uint8_t *psk      = (chanIdx == 1) ? s_psk1      : s_psk0;
  size_t         pskLen   = (chanIdx == 1) ? s_pskLen1   : s_pskLen0;
  uint8_t        chanHash = (chanIdx == 1) ? s_chanHash1 : s_chanHash0;

  uint32_t pktId = esp_random();
  if (pktId == 0) pktId = 1;

  // --- cifratura AES-128 o AES-256 CTR ---
  uint8_t nonce[16] = {0};
  uint64_t id64 = pktId;
  memcpy(nonce, &id64, 8);
  memcpy(nonce + 8, &s_nodeId, 4);

  uint8_t cipher[208];
  aesEncrypt(cipher, plain, plainLen, psk, pskLen, nonce);

  // --- header 16 byte (RadioInterface.h) ---
  uint8_t pkt[224];
  uint32_t dest = BROADCAST_ADDR;
  memcpy(pkt + 0,  &dest,     4);
  memcpy(pkt + 4,  &s_nodeId, 4);
  memcpy(pkt + 8,  &pktId,    4);
  const uint8_t hopLimit = MESH_HOP_LIMIT, hopStart = MESH_HOP_LIMIT;
  pkt[12] = (hopLimit & 0x07) | (uint8_t)(hopStart << 5);   // flags
  pkt[13] = chanHash;                                        // channel hash
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
    Serial.printf("[mesh] TX ok ch%d id=0x%08lx %u byte\n",
                  chanIdx, (unsigned long)pktId, (unsigned)(16 + plainLen));
    return true;
  }
  Serial.printf("[mesh] TX fallita: %d\n", st);
  return false;
}

// ---------------------------------------------------------------------------
// Incapsula Data { portnum, payload } e trasmette sul canale specificato.
// ---------------------------------------------------------------------------
static bool sendData(uint8_t portnum, const uint8_t *payload, size_t len,
                     uint8_t chanIdx = 0) {
  uint8_t data[220];
  size_t p = 0;
  p = pbVarintField(data, p, 1, portnum);
  p = pbBytesField(data, p, 2, payload, len);
#if MESH_OK_TO_MQTT
  p = pbVarintField(data, p, 9, 1);   // bitfield campo 9: bit 0 = ok_to_mqtt
#endif
  return meshTransmit(data, p, chanIdx);
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
  // time=0 viene omesso dal protobuf (valore default) — usato quando
  // l'orologio non è ancora sincronizzato, evitando di trasmettere 1970-01-01.
  uint8_t tel[80];
  size_t t = 0;
  t = pbFixed32Field(tel, t, 1, timeSyncValid() ? (uint32_t)time(nullptr) : 0);
  t = pbBytesField(tel, t, 3, env, e);

  return sendData(PORT_TELEMETRY, tel, t, 0);   // sempre canale principale
}

// ---------------------------------------------------------------------------
bool meshSendNodeInfo() {
  // User { id=1 string; long_name=2 string; short_name=3 string;
  //        hw_model=5 varint; is_unmessagable=9 bool }
  // NB: hw_model è il field 5 (non 6: il 6 è is_licensed). Inviarlo come 6
  //     impostava per errore is_licensed e lasciava hw_model non valorizzato.
  // Riferimenti (mesh.pb.h): meshtastic_User_hw_model_tag = 5,
  //     meshtastic_User_is_licensed_tag = 6, meshtastic_User_is_unmessagable_tag = 9
  char nodeIdStr[12];
  snprintf(nodeIdStr, sizeof(nodeIdStr), "!%08lx", (unsigned long)s_nodeId);

  uint8_t user[120];
  size_t u = 0;
  u = pbBytesField(user, u, 1, (const uint8_t *)nodeIdStr,   strlen(nodeIdStr));
  u = pbBytesField(user, u, 2, (const uint8_t *)MESH_LONG_NAME, strlen(MESH_LONG_NAME));
  u = pbBytesField(user, u, 3, (const uint8_t *)s_shortName, strlen(s_shortName));
  u = pbVarintField(user, u, 5, 43);   // hw_model: HW_MODEL_HELTEC_V3 = 43
  u = pbVarintField(user, u, 9, 1);    // is_unmessagable = true: nodo TX-only, non risponde ai DM

  return sendData(PORT_NODEINFO, user, u, 0);
}

// ---------------------------------------------------------------------------
bool meshSendPosition() {
#if !MESH_POS_ENABLED
  return false;
#endif
  // Position { sfixed32 latitude_i=1; sfixed32 longitude_i=2; int32 altitude=3;
  //            fixed32 time=4 }
  // NB: in mesh.proto il campo time della Position è il field 4 (non 9: il 9 è
  // altitude_hae, sint32). Usarlo come 9/fixed32 genera un mismatch di wire type
  // che fa scartare l'INTERO pacchetto Position al ricevitore.
  // sfixed32 usa wire type 5 (fixed 32 bit), stesso encoding di uint32_t
  uint8_t pos[40];
  size_t p = 0;
  int32_t latI = MESH_LAT_I, lonI = MESH_LON_I;
  p = pbFixed32Field(pos, p, 1, (uint32_t)latI);
  p = pbFixed32Field(pos, p, 2, (uint32_t)lonI);
  p = pbVarintField(pos, p, 3, (uint64_t)(uint32_t)MESH_ALT_M);
  // time: omesso (non scritto) se l'orologio non e' ancora sincronizzato,
  // per non trasmettere un timestamp 1970-01-01 che i ricevitori potrebbero filtrare.
  if (timeSyncValid())
    p = pbFixed32Field(pos, p, 4, (uint32_t)time(nullptr));
  // precision_bits=32: precisione piena (in Meshtastic 2.x e' richiesto > 0
  // affinche' la posizione venga mostrata sulla mappa dell'app).
  p = pbVarintField(pos, p, 21, 32);

  return sendData(PORT_POSITION, pos, p, 0);
}

// ---------------------------------------------------------------------------
// Messaggio di testo puro (UTF-8).
// chanIdx: 0 = canale principale (MediumFast),
//          1 = canale testo (default = METEOLAZIO)
// ---------------------------------------------------------------------------
bool meshSendText(const char *txt, uint8_t chanIdx) {
  size_t len = strlen(txt);
  if (len == 0 || len > 200) return false;
  return sendData(PORT_TEXT, (const uint8_t *)txt, len, chanIdx);
}
