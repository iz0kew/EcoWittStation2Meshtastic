// ============================================================================
// timesync.h — Sincronizzazione orario da rete Meshtastic (senza RTC)
//
// All'avvio la scheda resta in ascolto LoRa per TSYNC_WINDOW_MS (5 min).
// Ogni pacchetto Meshtastic ricevuto sul canale configurato viene decifrato
// e scansionato alla ricerca di un timestamp unix (campo time nelle Telemetry
// e nelle Position). Dopo aver ricevuto il primo timestamp valido, attendiamo
// almeno TSYNC_CONFIRM_MIN ulteriori conferme entro ±TSYNC_MAX_DELTA_S secondi
// prima di considerare l'orologio affidabile.
// ============================================================================
#pragma once
#include <Arduino.h>

// --- Parametri sincronizzazione (modificabili) ---
#define TSYNC_WINDOW_MS    (5UL * 60UL * 1000UL)   // finestra ascolto all'avvio: 5 min
#define TSYNC_CONFIRM_MIN  3                          // conferme minime per accettare l'orario
#define TSYNC_MAX_DELTA_S  120                        // scarto max accettabile tra campioni (s)

enum TimeSyncState {
  TS_WAITING,       // finestra attiva, nessun timestamp ancora
  TS_UNCONFIRMED,   // primo timestamp ricevuto, attesa conferme
  TS_CONFIRMED,     // orario confermato da TSYNC_CONFIRM_MIN campioni concordi
  TS_TIMEOUT        // finestra scaduta senza raggiungere la conferma
};

struct TimeSyncStatus {
  TimeSyncState state;
  uint32_t      firstEpoch;   // epoch del primo campione (0 se TS_WAITING/TS_TIMEOUT)
  uint8_t       confirms;     // conferme ricevute finora
  int32_t       secsLeft;     // secondi mancanti alla scadenza della finestra
};

// Chiamare da setup() DOPO meshInit() e DOPO SPI/radio init.
// Mette la radio in LoRa RX (stessa frequenza/preset del TX Meshtastic).
void timeSyncBegin();

// Restituisce true se il processo è concluso (confermato o timeout).
// Quando ritorna true la radio è già stata riportata in modalità FSK.
bool timeSyncDone();

// Da chiamare a ogni iterazione di loop() finché timeSyncDone() == false.
// Gestisce la ricezione dei pacchetti e il timeout della finestra.
void timeSyncTick();

// Stato corrente (per la schermata display).
TimeSyncStatus timeSyncGetStatus();

// true se l'orologio è stato settato con successo (state == TS_CONFIRMED).
bool timeSyncValid();
