# Changelog

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
