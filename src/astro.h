// ============================================================================
// astro.h — calcoli astronomici offline: alba/tramonto e fase lunare
// Usati dalla schermata SCR_TIME e dal bollettino testuale Meshtastic.
// Non richiede connessione internet: tutto calcolato dalla data/ora di sistema
// e dalle coordinate geografiche in user_config.h (MESH_LAT_I, MESH_LON_I).
// ============================================================================
#pragma once
#include <time.h>

// Orari alba/tramonto in ora locale (già corretti con il fuso di sistema)
struct SunTimes {
  int  riseH, riseM;   // ora alba
  int  setH,  setM;    // ora tramonto
  bool valid;           // false in caso di giorno/notte polare o clock non sync
};

// Restituisce true e riempie 'out' se coordinate configurate e clock sync.
bool astroGetSunTimes(SunTimes &out);

// Età della luna in giorni dall'ultima luna nuova (0 .. ~29.5)
float astroMoonAge();

// Percentuale di illuminazione del disco lunare (0.0 = nuova, 1.0 = piena)
float astroMoonIllum();

// Nome della fase in italiano (versione estesa, per messaggi Meshtastic)
const char *astroMoonPhaseName();

// Nome breve per il display OLED/TFT (max 10 caratteri ASCII)
const char *astroMoonPhaseShort();

// Emoji Unicode della fase (per messaggi Meshtastic)
const char *astroMoonPhaseEmoji();
