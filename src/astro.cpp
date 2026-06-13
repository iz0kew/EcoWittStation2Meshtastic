// ============================================================================
// astro.cpp — calcoli astronomici offline
//
// Alba/tramonto: algoritmo NOAA (basato su Jean Meeus "Astronomical Algorithms").
//   Accuratezza: ±1 minuto per latitudini 0°–65°, anni 1950–2150.
//
// Fase lunare: formula della luna sinodica con luna nuova di riferimento
//   (2000-01-06 18:14 UTC, JD 2451550.259). Accuratezza: ±1 giorno.
//
// La conversione UTC → ora locale usa l'offset già impostato da tzset()
// in timeSyncBegin(), senza richiedere nessuna tabella di timezone aggiuntiva.
// ============================================================================
#include "astro.h"
#include "user_config.h"
#include <math.h>
#include <time.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG (M_PI / 180.0)
#define RAD (180.0 / M_PI)

// Coordinate della stazione (gradi decimali)
static const double LAT = (double)MESH_LAT_I / 1e7;
static const double LON = (double)MESH_LON_I / 1e7;

// ---------------------------------------------------------------------------
// Offset locale rispetto a UTC in minuti (tiene conto dell'ora legale corrente)
// ---------------------------------------------------------------------------
static int tzOffsetMin() {
    time_t t = time(nullptr);
    // IMPORTANTE: localtime/gmtime restituiscono un puntatore statico → copiare
    struct tm lt = *localtime(&t);
    struct tm ut = *gmtime(&t);
    int off = (lt.tm_hour - ut.tm_hour) * 60 + (lt.tm_min - ut.tm_min);
    // Correzione cambio giorno (es. UTC 23:30 → locale 00:30 +1)
    int dd = lt.tm_mday - ut.tm_mday;
    if (dd >  1) dd = -1;
    if (dd < -1) dd =  1;
    off += dd * 24 * 60;
    return off;
}

// ---------------------------------------------------------------------------
// Numero di giorno giuliano (Julian Day Number) per una data calendario
// ---------------------------------------------------------------------------
static double julianDay(int y, int m, int d) {
    if (m <= 2) { y--; m += 12; }
    int A = y / 100, B = 2 - A + A / 4;
    return (int)(365.25 * (y + 4716)) + (int)(30.6001 * (m + 1)) + d + B - 1524.5;
}

// ---------------------------------------------------------------------------
// Calcola un singolo evento (alba o tramonto) per la data fornita.
// Ritorna minuti UTC dalla mezzanotte, oppure:
//   -1  = il sole non sorge mai (notte polare)
//   -2  = il sole non tramonta mai (giorno polare)
// ---------------------------------------------------------------------------
static int sunEvent(int year, int month, int day, bool rising) {
    double JD    = julianDay(year, month, day) + 0.5;  // noon JD
    double n     = JD - 2451545.0;  // giorni da J2000.0 (mezzogiorno solare)
    double Jstar = n - LON / 360.0;                        // mezzogiorno solare approx (Julian)

    // Anomalia media del Sole
    double M     = fmod(357.5291 + 0.98560028 * Jstar, 360.0);
    // Equazione del centro (correzione prima armonica)
    double C     = 1.9148 * sin(M * DEG) + 0.0200 * sin(2.0 * M * DEG)
                 + 0.0003 * sin(3.0 * M * DEG);
    // Longitudine eclittica del Sole
    double lam   = fmod(M + C + 180.0 + 102.9372, 360.0);
    // Transito solare (mezzogiorno vero, Julian date)
    double Jtr   = 2451545.0 + Jstar
                 + 0.0053 * sin(M * DEG)
                 - 0.0069 * sin(2.0 * lam * DEG);
    // Declinazione del Sole
    double sinDec = sin(lam * DEG) * sin(23.4397 * DEG);
    double cosDec = cos(asin(sinDec));

    // Angolo orario al sorgere/tramontare (zenith ufficiale = 90.833°)
    const double zenith = 90.833 * DEG;
    double cosOmega = (cos(zenith) - sin(LAT * DEG) * sinDec)
                    / (cos(LAT * DEG) * cosDec);

    if (cosOmega >  1.0) return -1;   // mai sorge
    if (cosOmega < -1.0) return -2;   // mai tramonta

    double omega  = acos(cosOmega) * RAD;   // gradi
    double Jev    = rising ? (Jtr - omega / 360.0) : (Jtr + omega / 360.0);

    // Da Julian Date a minuti UTC dalla mezzanotte:
    //   JD 0.0 = mezzogiorno → ora UTC = (fraz + 0.5) * 24 mod 24
    double frac   = Jev - floor(Jev);
    double utcH   = fmod((frac + 0.5) * 24.0, 24.0);
    return (int)(utcH * 60.0 + 0.5) % (24 * 60);
}

// ---------------------------------------------------------------------------
// API pubblica — alba e tramonto
// ---------------------------------------------------------------------------
bool astroGetSunTimes(SunTimes &out) {
    out.valid = false;
#if !MESH_POS_ENABLED
    return false;
#endif
    time_t now = time(nullptr);
    if (now < 1000000UL) return false;   // orologio non sincronizzato

    struct tm ut = *gmtime(&now);
    int year  = ut.tm_year + 1900;
    int month = ut.tm_mon  + 1;
    int day   = ut.tm_mday;

    int riseUtc = sunEvent(year, month, day, true);
    int setUtc  = sunEvent(year, month, day, false);
    if (riseUtc < 0 || setUtc < 0) return false;

    int tzOff     = tzOffsetMin();
    int riseLoc   = ((riseUtc + tzOff) % (24 * 60) + 24 * 60) % (24 * 60);
    int setLoc    = ((setUtc  + tzOff) % (24 * 60) + 24 * 60) % (24 * 60);

    out.riseH = riseLoc / 60;  out.riseM = riseLoc % 60;
    out.setH  = setLoc  / 60;  out.setM  = setLoc  % 60;
    out.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// API pubblica — fase lunare
// ---------------------------------------------------------------------------
float astroMoonAge() {
    time_t now = time(nullptr);
    if (now < 1000000UL) return 0;
    struct tm ut = *gmtime(&now);
    double JD = julianDay(ut.tm_year + 1900, ut.tm_mon + 1, ut.tm_mday)
              + ut.tm_hour / 24.0 + ut.tm_min / 1440.0;
    // Luna nuova di riferimento: 2000-01-06 18:14 UTC → JD 2451550.259
    const double REF     = 2451550.259;
    const double SYNODIC = 29.53058867;
    double age = fmod(JD - REF, SYNODIC);
    if (age < 0) age += SYNODIC;
    return (float)age;
}

float astroMoonIllum() {
    float age = astroMoonAge();
    const float S = 29.53058867f;
    return (1.0f - cosf(2.0f * (float)M_PI * age / S)) * 0.5f;
}

// Indice di fase 0–7 (0=nuova, 4=piena)
static int moonIdx() {
    float age = astroMoonAge();
    const float S = 29.53058867f;
    if (age < S * 0.0625f || age >= S * 0.9375f) return 0;
    if (age < S * 0.1875f) return 1;
    if (age < S * 0.3125f) return 2;
    if (age < S * 0.4375f) return 3;
    if (age < S * 0.5625f) return 4;
    if (age < S * 0.6875f) return 5;
    if (age < S * 0.8125f) return 6;
    return 7;
}

static const char *NAMES_FULL[]  = {
    "Luna nuova", "Falce crescente", "Primo quarto", "Gibbosa crescente",
    "Luna piena", "Gibbosa calante", "Ultimo quarto", "Falce calante"
};
static const char *NAMES_SHORT[] = {
    "Nuova",  "F.Cresc.", "1.Quarto", "Gibb.+",
    "Piena",  "Gibb.-",  "Ult.Q.",   "F.Cal."
};
static const char *EMOJI[] = {
    "🌑", "🌒", "🌓", "🌔", "🌕", "🌖", "🌗", "🌘"
};

const char *astroMoonPhaseName()  { return NAMES_FULL[moonIdx()];  }
const char *astroMoonPhaseShort() { return NAMES_SHORT[moonIdx()]; }
const char *astroMoonPhaseEmoji() { return EMOJI[moonIdx()];       }
