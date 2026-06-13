# EcoWittStation2Meshtastic — v1.1.1

<p align="center">
  <strong>🇮🇹 <a href="#italiano">Italiano</a></strong> &nbsp;|&nbsp;
  <strong>🇬🇧 <a href="#english">English</a></strong>
</p>

---

<a name="italiano"></a>
## 🇮🇹 Italiano

Firmware custom per schede **Heltec ESP32 LoRa** che riceve i dati di una stazione meteo **Fine Offset / Ecowitt** via FSK 868 MHz e li pubblica sulla rete **Meshtastic** come telemetria ambientale standard, con display multi-schermata e storico grafico locale.

### Funzionalità

- **Ricezione FSK 868 MHz** dei sensori Ecowitt / Fine Offset:
  - 🌡️ **WH32** — temperatura e umidità (TX ogni ~64 s)
  - 🌧️ **WH40** — pluviometro con contatore cumulativo (TX ogni ~64 s)
  - ⚡ **WH57** — rilevatore fulmini AS3935 (TX ogni ~79 s)

- **Display multi-schermata** con navigazione tramite tasto PRG:

  | # | Schermata |
  |---|-----------|
  | 1 | Panoramica (tutti i sensori) |
  | 2 | WH32 — temperatura e umidità |
  | 3 | WH40 — pioggia (1h / 24h) |
  | 4 | WH57 — fulmini (stato, distanza, conteggio) |
  | 5 | Grafico temperatura 24h |
  | 6 | Grafico umidità 24h |
  | 7 | Grafico pioggia 24h (barre) |
  | 8 | Stato nodo Meshtastic |

- **Storico circolare** in RAM: campionatura ogni 5–10 min, finestra 24 ore

- **Bridge Meshtastic** (opzionale, configurabile):
  - Telemetria ambientale `EnvironmentMetrics` (temperatura media, umidità media, pioggia 1h/24h) → visibile nei grafici "Ambiente" dell'app
  - Posizione fissa (lat/lon/quota) → il nodo appare sulla mappa
  - NodeInfo con short/long name personalizzabili (lo short name appare anche sulla schermata 8)
  - Canale configurabile: nome e chiave PSK (base64) impostabili in `settings.ini`
  - Hop limit configurabile (0–7)
  - Avviso fulmini come messaggio di testo sul canale (opzionale)
  - Bollettino meteo testuale periodico sul canale (opzionale, intervallo separato)

> La radio è una sola: la ricezione FSK si sospende per ~1–2 secondi durante ogni trasmissione LoRa, poi riprende. Con sensori che trasmettono ogni 64–79 s la perdita di campioni è trascurabile.

### Hardware supportato

| Scheda | Display | Note |
|---|---|---|
| **Heltec WiFi LoRa 32 V3** | OLED 128×64 SSD1306 | scheda di sviluppo e test |
| **Heltec WiFi LoRa 32 V4** | OLED 128×64 SSD1306 | pin-compatibile V3, PA 28 dBm |
| **Heltec Wireless Tracker** | TFT 160×80 ST7735 | |

Tutte e tre usano **ESP32-S3 + SX1262**.

### Prerequisiti

- [PlatformIO](https://platformio.org/) (CLI o estensione VS Code)
- Python 3.7+
- Antenna collegata **prima** di alimentare la scheda

Le librerie vengono scaricate automaticamente al primo build:

| Libreria | Scopo |
|---|---|
| `jgromes/RadioLib ^7.1.2` | Driver SX1262 (FSK + LoRa) |
| `olikraus/U8g2 ^2.36.5` | Driver OLED SSD1306 |
| `rweather/Crypto ^0.4.0` | AES128-CTR per cifratura Meshtastic |
| `adafruit/Adafruit ST7735 and ST7789 Library ^1.10.4` | Driver TFT |
| `adafruit/Adafruit GFX Library ^1.11.11` | Grafica TFT |

### Installazione

```bash
git clone https://github.com/tuoutente/EcoWittStation2Meshtastic.git
cd EcoWittStation2Meshtastic

# 1. Personalizza settings.ini (vedi sezione Configurazione)
nano settings.ini

# 2. Compila e carica (scegli l'ambiente corretto)
pio run -e heltec_v3 -t upload
pio run -e heltec_v4 -t upload
pio run -e wireless_tracker -t upload

# Monitor seriale
pio device monitor
```

Lo script `tools/apply_settings.py` viene eseguito **automaticamente prima di ogni build** e genera `include/user_config.h`. Può essere lanciato a mano per verificare la configurazione:

```bash
python tools/apply_settings.py
```

### Configurazione — `settings.ini`

Questo è l'**unico file da modificare**. Non toccare `include/user_config.h`: viene sovrascritto ad ogni build.

```ini
[meshtastic]
enabled = true                    # abilita il bridge Meshtastic
frequency_mhz = 869.525           # frequenza TX LoRa (MHz)
preset = MediumFast               # preset modem LoRa
                                  # ShortTurbo | ShortFast | ShortSlow |
                                  # MediumFast | MediumSlow |
                                  # LongFast | LongModerate | LongSlow
send_interval_min = 10            # intervallo tra invii telemetria + NodeInfo (minuti)
text_interval_min = 30            # bollettino meteo testo sul canale (0 = disabilitato)
short_name = WX32                 # nome breve sulla rete (max 4 caratteri)
long_name = Stazione Meteo        # nome esteso sulla rete
tx_power_dbm = 14                 # potenza TX in dBm (-9..22 per SX1262)
hop_limit = 3                     # hop massimi in mesh (0–7)
channel_name =                    # nome canale (vuoto = nome preset, es. "MediumFast")
channel_key = AQ==                # PSK canale in base64 (AQ== = chiave pubblica default)
latitude = 41.8603                # coordinate fisse (0/0 = non trasmettere)
longitude = 13.0337
altitude_m = 571                  # quota in metri s.l.m.
lightning_text = true             # abilita avvisi fulmini sul canale (true/false)
lightning_window_min = 5          # finestra temporale per il conteggio fulmini (minuti)
lightning_threshold = 0.5         # soglia allarme: fulmini_nella_finestra / distanza_km

[ricezione]
freq_mhz = 868.35                 # frequenza RX sensori
bitrate_kbps = 17.24              # bitrate FSK (WH32: 58 µs → 17.24 kbps)
freq_dev_khz = 60.0               # deviazione FSK del trasmettitore
rx_bw_khz = 234.3                 # banda di ricezione

[storico]
sample_interval_min = 10          # campionatura per grafici (5 o 10 minuti)
```

**Canale personalizzato:** imposta `channel_name` e genera una chiave a 16 byte con:
```bash
python -c "import os,base64; print(base64.b64encode(os.urandom(16)).decode())"
```

Lo script valida tutti i valori e blocca la compilazione con un messaggio chiaro in caso di errore.

### Come appare sulla rete Meshtastic

1. **Avvio**: nodo visibile come `!xxxxxxxx` (ID derivato dal MAC WiFi)
2. **Ogni `send_interval_min`** (es. ogni 10 min):
   - `EnvironmentMetrics` con media temperatura/umidità e pioggia 1h/24h
   - **+30 secondi dopo**: NodeInfo (short/long name) e posizione fissa → il nodo compare con nome e sulla mappa
3. **Ogni `text_interval_min`** (se > 0): bollettino testo sul canale, es.:
   ```
   Stazione Meteo Olevano
   🌡️ 22.5°C  💧 65%
   🌧️ 1h 0.0mm  24h 2.3mm
   ```
4. **Se `lightning_text = true`**: allarme fulmini basato su punteggio soglia:
   - Ad ogni pacchetto WH57 (~79 s) si calcola `score = fulmini_negli_ultimi_N_min / distanza_km`
   - Se `score ≥ lightning_threshold` scatta l'avviso sul canale (cooldown = 1 finestra):
   ```
   ⚡ Stazione Meteo Olevano
   8 fulmini rilevati  ~15 km
   ```
   - Esempi equivalenti con soglia 0.5: 10 fulmini@20 km, 5@10 km, 20@40 km

### Note hardware

**Heltec V4 — attenzione alla potenza TX**
Il PA esterno porta l'uscita fino a 28 dBm oltre il valore impostato sul chip. Con `tx_power_dbm = 14` l'uscita reale supera il limite ERP della sotto-banda 869,4–869,65 MHz (25 mW / 14 dBm e.r.p. in Italia). Usa valori bassi: `0`–`8` dBm sono una scelta più cauta.

**Wireless Tracker — display invertito**
Se lo sfondo appare bianco invece che nero, commenta questa riga in `src/display_tft.cpp`:
```cpp
// tft.invertDisplay(true);
```

**Bitrate FSK — WH40 / WH57 non decodificano**
WH32 usa 58 µs (~17,24 kbps); WH40 e WH57 usano 56 µs (~17,86 kbps). Se WH40 o WH57 non vengono decodificati, prova:
```ini
bitrate_kbps = 17.5
```

### Struttura del progetto

```
EcoWittStation2Meshtastic/
├── settings.ini                  ← configurazione utente (modifica qui)
├── platformio.ini
├── LICENSE
├── tools/
│   └── apply_settings.py         ← genera user_config.h prima del build
├── include/
│   └── user_config.h             ← generato automaticamente, non editare
└── src/
    ├── main.cpp                  loop principale, time-sharing FSK / LoRa
    ├── board_config.h            pin per ogni scheda
    ├── sensors.h / .cpp          decoder WH32, WH40, WH57
    ├── history.h                 ring buffer 24h + finestre pioggia
    ├── display.h                 primitive grafiche (spazio logico 128×64)
    ├── display_oled.cpp          backend SSD1306 (V3 / V4)
    ├── display_tft.cpp           backend ST7735 160×80 (Wireless Tracker)
    ├── screens.cpp               le 8 schermate
    └── meshtastic_tx.h / .cpp    protocollo Meshtastic: protobuf + AES128-CTR
```

### Riferimenti tecnici

Decoder sensori ricavati dai sorgenti di **[rtl_433](https://github.com/merbanan/rtl_433)**:
`fineoffset.c` (WH32, protocollo 78), `ambientweather_wh31e.c` (WH40, protocollo 113), `fineoffset_wh31l.c` (WH57/WH31L, protocollo 190).

Protocollo Meshtastic implementato verificando **[meshtastic/firmware](https://github.com/meshtastic/firmware)** e **[meshtastic/protobufs](https://github.com/meshtastic/protobufs)**:
PSK default (`Channels.h`), hash canale (`Channels.cpp`), header radio e flag hop (`RadioInterface.h`), nonce AES-CTR (`CryptoEngine.cpp`), campi protobuf (`mesh.proto`, `telemetry.proto`, `portnums.proto`).

### Licenza

Distribuito sotto licenza **MIT** — vedi [LICENSE](LICENSE).

---

<a name="english"></a>
## 🇬🇧 English

Custom firmware for **Heltec ESP32 LoRa** boards that receives data from a **Fine Offset / Ecowitt** weather station via FSK 868 MHz and publishes it to the **Meshtastic** network as standard ambient telemetry, with a multi-screen display and local graphing.

### Features

- **FSK 868 MHz reception** of Ecowitt / Fine Offset sensors:
  - 🌡️ **WH32** — temperature and humidity (TX every ~64 s)
  - 🌧️ **WH40** — rain gauge with cumulative counter (TX every ~64 s)
  - ⚡ **WH57** — AS3935 lightning detector (TX every ~79 s)

- **Multi-screen display** with navigation via the PRG button:

  | # | Screen |
  |---|--------|
  | 1 | Overview (all sensors) |
  | 2 | WH32 — temperature & humidity |
  | 3 | WH40 — rainfall (1h / 24h) |
  | 4 | WH57 — lightning (state, distance, count) |
  | 5 | Temperature graph 24h |
  | 6 | Humidity graph 24h |
  | 7 | Rainfall graph 24h (bar chart) |
  | 8 | Meshtastic node status |

- **Circular history** in RAM: one sample every 5–10 min, 24-hour window

- **Meshtastic bridge** (optional, fully configurable):
  - `EnvironmentMetrics` telemetry (average temperature, humidity, rainfall 1h/24h) → visible in the app's "Environment" graphs
  - Fixed position (lat/lon/altitude) → node appears on the map
  - NodeInfo with configurable short/long name (short name also shown on screen 8)
  - Configurable channel: name and PSK key (base64) set in `settings.ini`
  - Configurable hop limit (0–7)
  - Lightning alert as a text message on the channel (optional)
  - Periodic human-readable weather bulletin on the channel (optional, separate interval)

> There is only one radio: FSK reception pauses for ~1–2 seconds during each LoRa transmission, then resumes. With sensors transmitting every 64–79 s the chance of missing a sample is negligible.

### Supported hardware

| Board | Display | Notes |
|---|---|---|
| **Heltec WiFi LoRa 32 V3** | OLED 128×64 SSD1306 | development & test board |
| **Heltec WiFi LoRa 32 V4** | OLED 128×64 SSD1306 | pin-compatible with V3, 28 dBm PA |
| **Heltec Wireless Tracker** | TFT 160×80 ST7735 | |

All three use **ESP32-S3 + SX1262**.

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3.7+
- Antenna connected **before** powering the board

Libraries are downloaded automatically on the first build:

| Library | Purpose |
|---|---|
| `jgromes/RadioLib ^7.1.2` | SX1262 driver (FSK + LoRa) |
| `olikraus/U8g2 ^2.36.5` | OLED SSD1306 driver |
| `rweather/Crypto ^0.4.0` | AES128-CTR for Meshtastic encryption |
| `adafruit/Adafruit ST7735 and ST7789 Library ^1.10.4` | TFT driver |
| `adafruit/Adafruit GFX Library ^1.11.11` | TFT graphics |

### Installation

```bash
git clone https://github.com/yourusername/EcoWittStation2Meshtastic.git
cd EcoWittStation2Meshtastic

# 1. Customise settings.ini (see Configuration section)
nano settings.ini

# 2. Build and flash (choose the right environment)
pio run -e heltec_v3 -t upload
pio run -e heltec_v4 -t upload
pio run -e wireless_tracker -t upload

# Serial monitor
pio device monitor
```

The script `tools/apply_settings.py` runs **automatically before every build** and generates `include/user_config.h`. It can also be run manually to check the configuration without building:

```bash
python tools/apply_settings.py
```

### Configuration — `settings.ini`

This is the **only file you need to edit**. Do not touch `include/user_config.h`: it is overwritten on every build.

```ini
[meshtastic]
enabled = true                    # enable the Meshtastic bridge
frequency_mhz = 869.525           # LoRa TX frequency (MHz)
preset = MediumFast               # LoRa modem preset
                                  # ShortTurbo | ShortFast | ShortSlow |
                                  # MediumFast | MediumSlow |
                                  # LongFast | LongModerate | LongSlow
send_interval_min = 10            # telemetry + NodeInfo send interval (minutes)
text_interval_min = 30            # weather text bulletin on channel (0 = disabled)
short_name = WX32                 # node short name on the mesh (max 4 chars)
long_name = Weather Station       # node long name on the mesh
tx_power_dbm = 14                 # TX power in dBm (-9..22 for SX1262)
hop_limit = 3                     # max hops in mesh (0–7)
channel_name =                    # channel name (empty = preset name, e.g. "MediumFast")
channel_key = AQ==                # channel PSK in base64 (AQ== = default public key)
latitude = 41.8603                # fixed coordinates (0/0 = do not broadcast)
longitude = 13.0337
altitude_m = 571                  # altitude in metres above sea level
lightning_text = true             # enable lightning alerts on channel (true/false)
lightning_window_min = 5          # rolling time window for strike count (minutes)
lightning_threshold = 0.5         # alert threshold: strikes_in_window / distance_km

[ricezione]
freq_mhz = 868.35                 # sensor RX frequency
bitrate_kbps = 17.24              # FSK bit rate (WH32: 58 µs → 17.24 kbps)
freq_dev_khz = 60.0               # transmitter FSK deviation
rx_bw_khz = 234.3                 # receive bandwidth

[storico]
sample_interval_min = 10          # graph sampling interval (5 or 10 minutes)
```

**Custom channel:** set `channel_name` and generate a 16-byte key with:
```bash
python -c "import os,base64; print(base64.b64encode(os.urandom(16)).decode())"
```

The script validates all values and halts the build with a clear error message if something is out of range.

### How it appears on the Meshtastic network

1. **On boot**: node visible as `!xxxxxxxx` (ID derived from the WiFi MAC)
2. **Every `send_interval_min`** (e.g. every 10 min):
   - `EnvironmentMetrics` with average temperature/humidity and rainfall 1h/24h
   - **+30 seconds later**: NodeInfo (short/long name) and fixed position → node appears with name and on the map
3. **Every `text_interval_min`** (if > 0): text bulletin on the channel, e.g.:
   ```
   Stazione Meteo Olevano
   🌡️ 22.5°C  💧 65%
   🌧️ 1h 0.0mm  24h 2.3mm
   ```
4. **If `lightning_text = true`**: score-based lightning alert:
   - Every WH57 packet (~79 s): `score = strikes_in_last_N_min / distance_km`
   - If `score ≥ lightning_threshold`, a channel alert is sent (cooldown = 1 window):
   ```
   ⚡ Stazione Meteo Olevano
   8 fulmini rilevati  ~15 km
   ```
   - Equivalent examples at threshold 0.5: 10 strikes@20 km, 5@10 km, 20@40 km

### Hardware notes

**Heltec V4 — TX power warning**
The external PA boosts output up to 28 dBm beyond the value set on the chip. With `tx_power_dbm = 14` the actual output exceeds the ERP limit of the 869.4–869.65 MHz sub-band (25 mW / 14 dBm e.r.p.). Use lower values: `0`–`8` dBm is a safer choice.

**Wireless Tracker — inverted display**
If the background appears white instead of black, comment out this line in `src/display_tft.cpp`:
```cpp
// tft.invertDisplay(true);
```

**FSK bit rate — WH40 / WH57 not decoding**
The WH32 uses 58 µs bits (~17.24 kbps); the WH40 and WH57 use 56 µs (~17.86 kbps). If WH40 or WH57 frames are not decoded, try:
```ini
bitrate_kbps = 17.5
```

### Project structure

```
EcoWittStation2Meshtastic/
├── settings.ini                  ← user configuration (edit this file)
├── platformio.ini
├── LICENSE
├── tools/
│   └── apply_settings.py         ← generates user_config.h before each build
├── include/
│   └── user_config.h             ← auto-generated, do not edit
└── src/
    ├── main.cpp                  main loop, FSK / LoRa time-sharing
    ├── board_config.h            pin definitions per board
    ├── sensors.h / .cpp          WH32, WH40, WH57 decoders
    ├── history.h                 24h ring buffer + rainfall windows
    ├── display.h                 graphics primitives (128×64 logical space)
    ├── display_oled.cpp          SSD1306 backend (V3 / V4)
    ├── display_tft.cpp           ST7735 160×80 backend (Wireless Tracker)
    ├── screens.cpp               the 8 screens
    └── meshtastic_tx.h / .cpp    Meshtastic protocol: protobuf + AES128-CTR
```

### Technical references

Sensor decoders derived from **[rtl_433](https://github.com/merbanan/rtl_433)** sources:
`fineoffset.c` (WH32, protocol 78), `ambientweather_wh31e.c` (WH40, protocol 113), `fineoffset_wh31l.c` (WH57/WH31L, protocol 190).

Meshtastic protocol implemented by inspecting **[meshtastic/firmware](https://github.com/meshtastic/firmware)** and **[meshtastic/protobufs](https://github.com/meshtastic/protobufs)**:
default PSK (`Channels.h`), channel hash algorithm (`Channels.cpp`), radio header and hop flags (`RadioInterface.h`), AES-CTR nonce (`CryptoEngine.cpp`), protobuf fields (`mesh.proto`, `telemetry.proto`, `portnums.proto`).

### License

Released under the **MIT License** — see [LICENSE](LICENSE).

---

### Changelog

#### v1.1.1
- Added `ok_to_mqtt` setting: when enabled, sets bit 0 of the `bitfield` field (tag 9) in the `Data` protobuf payload, allowing MQTT gateway nodes to forward packets to the broker.

#### v1.1.0
- Initial public release.

---

<p align="center">Made with ☕ and LoRa waves</p>
