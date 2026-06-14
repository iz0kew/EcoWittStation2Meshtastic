# Changelog

## [1.4.4] — 2026-06-14

### Corretto
- **timesync: "poison first sample"** (`src/timesync.cpp`): il primo timestamp
  ricevuto non veniva più applicato immediatamente all'orologio di sistema.
  Veniva memorizzato come riferimento (`s_firstEpoch`) e applicato solo dopo
  aver raccolto `TSYNC_CONFIRM_MIN` conferme concordi. Questo evita che un
  campo protobuf casuale nel range 2020–2050 invalidi tutti i campioni
  successivi bloccando la sincronizzazione in `TS_UNCONFIRMED` per tutta la
  finestra di 5 minuti.
- **timesync: confronto conferme su `s_firstEpoch`** (`src/timesync.cpp`): i
  campioni successivi al primo vengono ora confrontati con `s_firstEpoch`
  invece che con `time(nullptr)` (che era 0 finché `applyEpoch()` non veniva
  chiamata).
- **timesync: doppia chiamata a `radioStartFSK()`** (`src/timesync.cpp`):
  rimossa la chiamata a `radioStartFSK()` da `timeSyncTick()` (sia in caso di
  conferma che di timeout). Il cambio di modalità radio LoRa→FSK è ora
  responsabilità esclusiva di `main.cpp` tramite il blocco `fskStarted`,
  evitando una doppia inizializzazione della radio.
- **meshtastic_tx: timestamp falso in assenza di orologio** (`src/meshtastic_tx.cpp`):
  `Telemetry.time` e `Position.time` ora vengono impostati a `0` (campo omesso
  in protobuf) quando `timeSyncValid()` è falso, evitando di trasmettere date
  tipo `1970-01-01 00:05:xx` sui nodi della rete.
- **screens: ora spazzatura in `TS_UNCONFIRMED`** (`src/screens.cpp`): la
  schermata orario non chiama più `time(nullptr)` in stato `TS_UNCONFIRMED`
  (orologio non ancora settato). Mostra invece il primo campione grezzo
  (`ts.firstEpoch`) in UTC con prefisso `~` per indicare che è provvisorio.

### Aggiunto
- **NodeInfo `is_unmessagable`** (`src/meshtastic_tx.cpp`): il pacchetto
  NodeInfo include ora il campo `is_unmessagable = true` (field 9, protobuf
  Meshtastic). I nodi della rete che ricevono il NodeInfo sapranno che questa
  stazione è TX-only e non può ricevere messaggi diretti.

---

## [1.4.0] — 2025-06-13

### Aggiunto
- **Sincronizzazione orario dalla rete Meshtastic** (`src/timesync.h/.cpp`):
  la scheda ascolta la mesh in LoRa per 5 minuti all'avvio per ricevere un
  timestamp; nessun GPS né NTP necessari.
- **Fuso orario automatico** (`tools/apply_settings.py`): la stringa POSIX TZ
  viene derivata dalle coordinate di `settings.ini` tramite bounding-box
  geografici; ora legale inclusa per tutte le regioni principali.
- **Calcoli astronomici offline** (`src/astro.h/.cpp`):
  - Alba e tramonto con algoritmo NOAA (±1 min per lat 0°–65°)
  - Fase lunare: nome completo (crescente/calante), percentuale di illuminazione,
    emoji di fase
- **Scheduler astronomico**: il bollettino meteo viene inviato sul canale
  principale (MediumFast, ch0) automaticamente a alba+1h, mezzogiorno locale
  e tramonto−1h; il canale secondario (METEOLAZIO, ch1) usa il proprio
  `text_interval_min` indipendentemente.
- **Alba, tramonto e fase lunare nel bollettino testuale** Meshtastic.
- **Link GitHub abbreviato** in fondo a ogni bollettino (`tinyurl.com/26uu449o`).
- **Schermata SCR_TIME** (orario + data, senza sovraffollamento).
- **Schermata SCR_ASTRO** (effemeridi: alba, tramonto, nome fase lunare,
  percentuale di illuminazione) — accessibile ciclando con il tasto PRG.

### Modificato
- `settings.ini`: `timezone = auto` (default) sostituisce la stringa POSIX
  manuale; `text_interval_min` ora governa solo il canale secondario.
- `src/screens.cpp`: `drawTime()` ripristinata al solo orologio/data;
  dati astronomici spostati nella nuova `drawAstro()`.
- `src/main.cpp`: rimosso il ciclo fisso su ch0, aggiunto `checkAstroSend()`.
- `src/meshtastic_tx.h/.cpp`: `meshSendText()` accetta ora un parametro
  `chanIdx` (default 1 = canale testo); funzioni mancanti completate
  (`meshSendNodeInfo`, `meshSendPosition`, `meshSendTelemetry`).

---

## [1.2.0] — precedente

Release iniziale pubblica con supporto WH32/WH40/WH57, bridge telemetria
Meshtastic, doppio canale, display multi-schermata e grafici 24h.
