// ============================================================================
// timesync.cpp — Sincronizzazione orario da traffico Meshtastic
//
// Algoritmo:
//   1. radioStartLoRaRX() mette l'SX1262 in ricezione continua LoRa
//      con gli stessi parametri usati per la trasmissione Meshtastic.
//   2. Ogni pacchetto ricevuto:
//      a. Controlla il channel hash nell'header (byte 13): accetta solo i
//         pacchetti del canale principale o del canale testo configurati.
//      b. Decifra il payload con AES-128/256-CTR usando la PSK del canale.
//      c. Scansiona il Data protobuf decifrato cercando un campo fixed32 o
//         varint nel range [2020-01-01, 2050-01-01] — il timestamp unix.
//      d. Se trovato: aggiorna la state machine.
//   3. State machine:
//      TS_WAITING     → (primo timestamp) → settimeofday + TS_UNCONFIRMED
//      TS_UNCONFIRMED → (conferma ok)     → TS_UNCONFIRMED (confirms++)
//                     → (confirms >= MIN) → settimeofday + TS_CONFIRMED → FSK
//      TS_WAITING/TS_UNCONFIRMED → (timeout 5 min) → TS_TIMEOUT → FSK
// ============================================================================
#include "timesync.h"
#include "user_config.h"
#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <CTR.h>
#include <time.h>
#include <sys/time.h>
#include <cstring>   // per strlen

// Radio e ISR definiti in main.cpp
extern SX1262          radio;
extern volatile bool   rxFlag;
void radioStartFSK();
void radioStartLoRaRX();

// ---------------------------------------------------------------------------
// Costanti protocollo (specchio di meshtastic_tx.cpp)
// ---------------------------------------------------------------------------
static const uint8_t  LORA_SYNCWORD  = 0x2B;
static const uint16_t LORA_PREAMBLE  = 16;

// PSK canale principale e testo (da user_config.h generato da settings.ini)
static const uint8_t s_psk0[MESH_CHANNEL_KEY_SIZE]      = MESH_CHANNEL_KEY;
static const uint8_t s_psk1[MESH_TEXT_CHANNEL_KEY_SIZE] = MESH_TEXT_CHANNEL_KEY;

// ---------------------------------------------------------------------------
// Stato interno
// ---------------------------------------------------------------------------
static TimeSyncState s_state       = TS_WAITING;
static uint32_t      s_firstEpoch  = 0;
static uint8_t       s_confirms    = 0;
static uint32_t      s_windowEndMs = 0;
static uint8_t       s_chanHash0   = 0;
static uint8_t       s_chanHash1   = 0;

// ---------------------------------------------------------------------------
// Utilità: xor-hash di un buffer (stesso algoritmo di Channels.cpp Meshtastic)
// ---------------------------------------------------------------------------
static uint8_t xorHash(const uint8_t *p, size_t len) {
  uint8_t h = 0;
  for (size_t i = 0; i < len; i++) h ^= p[i];
  return h;
}

// ---------------------------------------------------------------------------
// AES-CTR: encrypt == decrypt. Supporta chiavi da 16 (AES-128) o 32 (AES-256) byte.
// ---------------------------------------------------------------------------
static void aesCrypt(uint8_t *out, const uint8_t *in, size_t len,
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
// Scanner protobuf ricorsivo: trova il primo valore fixed32 o varint che
// rientra nel range di un timestamp unix plausibile (2020–2050).
// Tratta i campi length-delimited come sotto-messaggi e ci ricorre dentro,
// in modo da trovare il timestamp anche quando è annidato (es. EnvironmentMetrics
// dentro Telemetry, o il campo time dentro Position).
// ---------------------------------------------------------------------------
static const uint32_t EPOCH_MIN = 1577836800UL;   // 2020-01-01 00:00:00 UTC
static const uint32_t EPOCH_MAX = 2524608000UL;   // 2050-01-01 00:00:00 UTC

static uint32_t pbScanTimestamp(const uint8_t *buf, size_t len) {
  size_t i = 0;
  while (i < len) {
    // Leggi il tag (varint: field_number<<3 | wire_type)
    uint64_t tag   = 0;
    uint8_t  shift = 0;
    while (i < len && shift < 64) {
      uint8_t b = buf[i++];
      tag |= (uint64_t)(b & 0x7F) << shift;
      shift += 7;
      if (!(b & 0x80)) break;
    }
    if (shift == 0) break;    // buffer vuoto

    uint8_t wire = (uint8_t)(tag & 0x07);

    switch (wire) {

      case 0: {  // varint
        uint64_t val   = 0;
        uint8_t  sh    = 0;
        while (i < len && sh < 64) {
          uint8_t b = buf[i++];
          val |= (uint64_t)(b & 0x7F) << sh;
          sh += 7;
          if (!(b & 0x80)) break;
        }
        if (val >= EPOCH_MIN && val <= EPOCH_MAX)
          return (uint32_t)val;
        break;
      }

      case 1:    // 64-bit fixed — skip
        if (i + 8 > len) return 0;
        i += 8;
        break;

      case 2: {  // length-delimited (bytes / embedded message / string)
        uint64_t l   = 0;
        uint8_t  sh  = 0;
        while (i < len && sh < 64) {
          uint8_t b = buf[i++];
          l |= (uint64_t)(b & 0x7F) << sh;
          sh += 7;
          if (!(b & 0x80)) break;
        }
        if (l > len - i) return 0;
        // Ricorre nel sotto-messaggio per trovare timestamp annidati
        uint32_t ts = pbScanTimestamp(buf + i, (size_t)l);
        if (ts) return ts;
        i += (size_t)l;
        break;
      }

      case 5: {  // 32-bit fixed
        if (i + 4 > len) return 0;
        uint32_t val;
        memcpy(&val, buf + i, 4);
        i += 4;
        if (val >= EPOCH_MIN && val <= EPOCH_MAX)
          return val;
        break;
      }

      default:   // wire type 3 (SGROUP), 4 (EGROUP), 6, 7: non usati in Meshtastic
        return 0;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Tenta di estrarre un timestamp unix da un pacchetto Meshtastic grezzo
// (header 16 byte + payload cifrato). Ritorna 0 se il pacchetto non è
// decifrabile con le nostre chiavi o non contiene un timestamp valido.
// ---------------------------------------------------------------------------
static uint32_t tryExtractTimestamp(const uint8_t *pkt, size_t len) {
  if (len < 17 || len > 256) return 0;

  // Byte 13 dell'header = channel hash (filtro rapido prima di decriptare)
  uint8_t chanHash = pkt[13];

  // fromNode e pktId per costruire il nonce AES-CTR
  uint32_t fromNode, pktId;
  memcpy(&fromNode, pkt + 4, 4);
  memcpy(&pktId,    pkt + 8, 4);

  // Nonce: pktId come uint64 LE nei byte 0-7, fromNode LE nei byte 8-11
  uint8_t nonce[16] = {0};
  uint64_t id64 = pktId;
  memcpy(nonce,     &id64,     8);
  memcpy(nonce + 8, &fromNode, 4);

  const uint8_t *cipher = pkt + 16;
  size_t          cLen  = len - 16;
  if (cLen == 0 || cLen > 208) return 0;

  // Prova i canali configurati nell'ordine: principale (0), testo/fulmini (1)
  struct { const uint8_t *psk; size_t pskLen; uint8_t hash; } channels[] = {
    { s_psk0, sizeof(s_psk0), s_chanHash0 },
    { s_psk1, sizeof(s_psk1), s_chanHash1 },
  };
  // Se i due canali hanno lo stesso hash, tentiamo solo una volta
  const int nChannels = (s_chanHash0 == s_chanHash1) ? 1 : 2;

  for (int ch = 0; ch < nChannels; ch++) {
    if (chanHash != channels[ch].hash) continue;   // hash non corrisponde: salta

    uint8_t plain[208];
    aesCrypt(plain, cipher, cLen, channels[ch].psk, channels[ch].pskLen, nonce);

    // Verifica di sanità: il primo byte del Data protobuf deve essere un tag
    // con field_number 1 o 2 e wire_type 0 o 2 (portnum varint o payload bytes).
    // field=1, wire=0 → tag=0x08   (portnum)
    // field=2, wire=2 → tag=0x12   (payload bytes, se portnum è 0 / omesso)
    uint8_t firstTag = plain[0];
    uint8_t firstField = firstTag >> 3;
    uint8_t firstWire  = firstTag & 0x07;
    if (firstField < 1 || firstField > 9) continue;
    if (firstWire != 0 && firstWire != 2 && firstWire != 5) continue;

    uint32_t ts = pbScanTimestamp(plain, cLen);
    if (ts) {
      Serial.printf("[tsync] pkt da !%08lx  ch%d  len=%u  ts=%lu\n",
                    (unsigned long)fromNode, ch, (unsigned)len, (unsigned long)ts);
      return ts;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Imposta l'orologio di sistema con settimeofday()
// ---------------------------------------------------------------------------
static void applyEpoch(uint32_t epoch) {
  struct timeval tv = { (time_t)epoch, 0 };
  settimeofday(&tv, nullptr);
}

// ---------------------------------------------------------------------------
// API pubblica
// ---------------------------------------------------------------------------

void timeSyncBegin() {
  s_state      = TS_WAITING;
  s_firstEpoch = 0;
  s_confirms   = 0;
  s_windowEndMs = millis() + TSYNC_WINDOW_MS;

  // Calcola channel hash per i due canali (stessa formula di Channels.cpp)
  s_chanHash0 = xorHash((const uint8_t *)MESH_CHANNEL_NAME, strlen(MESH_CHANNEL_NAME))
              ^ xorHash(s_psk0, sizeof(s_psk0));
  s_chanHash1 = xorHash((const uint8_t *)MESH_TEXT_CHANNEL_NAME, strlen(MESH_TEXT_CHANNEL_NAME))
              ^ xorHash(s_psk1, sizeof(s_psk1));

  // Imposta il fuso orario da user_config.h (generato da settings.ini).
  // setenv/tzset sono supportati dall'ESP-IDF e funzionano con localtime().
  setenv("TZ", MESH_TIMEZONE, 1);
  tzset();
  Serial.printf("[tsync] timezone: '%s'\n", MESH_TIMEZONE);

  Serial.printf("[tsync] avvio — finestra %lu s, hash ch0=0x%02x ch1=0x%02x\n",
                (unsigned long)(TSYNC_WINDOW_MS / 1000),
                s_chanHash0, s_chanHash1);

  radioStartLoRaRX();
}

bool timeSyncDone() {
  return s_state == TS_CONFIRMED || s_state == TS_TIMEOUT;
}

void timeSyncTick() {
  if (timeSyncDone()) return;

  uint32_t now = millis();

  // --- Controllo timeout finestra ---
  if ((int32_t)(now - s_windowEndMs) >= 0) {
    Serial.printf("[tsync] timeout — stato: %s\n",
                  s_state == TS_UNCONFIRMED ? "non confermato" : "nessun campione");
    s_state = TS_TIMEOUT;
    radioStartFSK();
    return;
  }

  // --- Pacchetto LoRa disponibile? ---
  if (!rxFlag) return;
  rxFlag = false;

  size_t pktLen = (size_t)radio.getPacketLength();
  if (pktLen < 17 || pktLen > 240) {
    radio.startReceive();
    return;
  }

  uint8_t buf[256];
  int st = radio.readData(buf, pktLen);   // size_t, nessun cast
  radio.startReceive();

  if (st != RADIOLIB_ERR_NONE) return;

  // --- Tenta estrazione timestamp ---
  uint32_t epoch = tryExtractTimestamp(buf, pktLen);
  if (epoch == 0) return;

  if (s_state == TS_WAITING) {
    // Primo campione valido: setta subito l'orologio e attendi conferme
    s_firstEpoch = epoch;
    s_confirms   = 1;
    s_state      = TS_UNCONFIRMED;
    applyEpoch(epoch);
    Serial.printf("[tsync] primo campione: %lu — in attesa di %d conferme\n",
                  (unsigned long)epoch, TSYNC_CONFIRM_MIN - 1);

  } else if (s_state == TS_UNCONFIRMED) {
    // Campione successivo: confronta con l'ora di sistema corrente
    // (che avanza dal primo campione, quindi è già "aggiustata")
    time_t nowEpoch   = time(nullptr);
    int32_t deltaS    = (int32_t)epoch - (int32_t)nowEpoch;

    if (deltaS < -TSYNC_MAX_DELTA_S || deltaS > TSYNC_MAX_DELTA_S) {
      Serial.printf("[tsync] campione rifiutato: delta %+lds (> %ds)\n",
                    (long)deltaS, TSYNC_MAX_DELTA_S);
      return;
    }

    s_confirms++;
    Serial.printf("[tsync] conferma %d/%d (delta %+lds)\n",
                  s_confirms, TSYNC_CONFIRM_MIN, (long)deltaS);

    if (s_confirms >= TSYNC_CONFIRM_MIN) {
      // Affina l'ora con l'ultimo campione confermato
      applyEpoch(epoch);
      s_state = TS_CONFIRMED;
      Serial.printf("[tsync] orario confermato: %lu\n", (unsigned long)epoch);
      radioStartFSK();
    }
  }
}

TimeSyncStatus timeSyncGetStatus() {
  uint32_t now = millis();
  int32_t secsLeft = (int32_t)(s_windowEndMs - now) / 1000;
  if (secsLeft < 0) secsLeft = 0;
  return { s_state, s_firstEpoch, s_confirms, secsLeft };
}

bool timeSyncValid() {
  return s_state == TS_CONFIRMED;
}
