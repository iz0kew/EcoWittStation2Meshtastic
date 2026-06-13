// ============================================================================
// screens.cpp — le schermate, cicliche col tasto PRG
// ============================================================================
#include "display.h"
#include "sensors.h"
#include "history.h"
#include "meshtastic_tx.h"
#include "user_config.h"
#include "timesync.h"
#include "astro.h"
#include <time.h>

// stato condiviso esposto da main.cpp
extern uint32_t g_fskOk, g_fskBad;
extern uint32_t g_nextMeshMs;     // millis() del prossimo invio mesh (0 = mai)

static int curScreen = SCR_OVERVIEW;

void screensNext() { curScreen = (curScreen + 1) % SCR_COUNT; }
void screensSet(int scr) { if (scr >= 0 && scr < SCR_COUNT) curScreen = scr; }

// ---------------------------------------------------------------------------
static void ageStr(char *out, size_t n, uint32_t lastSeen, bool valid) {
  if (!valid) { snprintf(out, n, "--"); return; }
  uint32_t s = (millis() - lastSeen) / 1000;
  if (s < 100)        snprintf(out, n, "%lus",  (unsigned long)s);
  else if (s < 6000)  snprintf(out, n, "%lum",  (unsigned long)(s / 60));
  else                snprintf(out, n, "%luh",  (unsigned long)(s / 3600));
}

static void header(const char *title) {
  gfxText(0, 10, title);
  char r[16];
  snprintf(r, sizeof(r), "%lu", (unsigned long)g_fskOk);
  gfxText(GFX_W - gfxTextWidth(r), 10, r);
  gfxLine(0, 12, GFX_W, 12);
}

// ---------------------------------------------------------------------------
static void drawOverview() {
  header("Meteo 868 FSK");
  char l[40], age[8];

  ageStr(age, sizeof(age), wh32.lastSeen, wh32.valid);
  if (wh32.valid)
    snprintf(l, sizeof(l), "T %5.1fC  %3u%%  %s", wh32.tempC, wh32.humidity, age);
  else
    snprintf(l, sizeof(l), "WH32: in attesa...");
  gfxText(0, 25, l);

  ageStr(age, sizeof(age), wh40.lastSeen, wh40.valid);
  if (wh40.valid) {
    float r24 = history.rainDeltaMm(wh40.rainMm, 24UL * 3600UL * 1000UL);
    snprintf(l, sizeof(l), "Pioggia 24h %.1fmm %s", r24, age);
  } else
    snprintf(l, sizeof(l), "WH40: in attesa...");
  gfxText(0, 38, l);

  ageStr(age, sizeof(age), wh57.lastSeen, wh57.valid);
  if (wh57.valid)
    snprintf(l, sizeof(l), "Fulmini %lu  %s", (unsigned long)wh57.strikesTotal, age);
  else
    snprintf(l, sizeof(l), "WH57: in attesa...");
  gfxText(0, 51, l);

#if MESH_ENABLED
  if (g_nextMeshMs) {
    int32_t left = (int32_t)(g_nextMeshMs - millis()) / 1000;
    if (left < 0) left = 0;
    snprintf(l, sizeof(l), "mesh TX in %ld:%02ld", (long)(left / 60), (long)(left % 60));
    gfxText(0, 63, l);
  }
#endif
}

// ---------------------------------------------------------------------------
static void drawWH32() {
  header("WH32 temp/umid");
  char l[40], age[8];
  if (!wh32.valid) { gfxText(0, 36, "Nessun dato"); return; }

  snprintf(l, sizeof(l), "%.1fC", wh32.tempC);
  gfxTextBig(0, 40, l);
  snprintf(l, sizeof(l), "%u%%", wh32.humidity);
  gfxText(GFX_W - gfxTextWidth(l), 40, l);

  ageStr(age, sizeof(age), wh32.lastSeen, true);
  snprintf(l, sizeof(l), "id %u  %s%s fa", wh32.id, wh32.battLow ? "BATT! " : "", age);
  gfxText(0, 53, l);
  snprintf(l, sizeof(l), "RSSI %.0f dBm", wh32.rssi);
  gfxText(0, 63, l);
}

// ---------------------------------------------------------------------------
static void drawWH40() {
  header("WH40 pioggia");
  char l[40], age[8];
  if (!wh40.valid) { gfxText(0, 36, "Nessun dato"); return; }

  float r1  = history.rainDeltaMm(wh40.rainMm, 3600UL * 1000UL);
  float r24 = history.rainDeltaMm(wh40.rainMm, 24UL * 3600UL * 1000UL);

  snprintf(l, sizeof(l), "%.1f", r24);
  gfxTextBig(0, 40, l);
  gfxText(GFX_W - gfxTextWidth("mm/24h"), 40, "mm/24h");

  snprintf(l, sizeof(l), "1h %.1fmm  tot %.1f", r1, wh40.rainMm);
  gfxText(0, 53, l);
  ageStr(age, sizeof(age), wh40.lastSeen, true);
  snprintf(l, sizeof(l), "batt %.1fV  RSSI %.0f  %s", wh40.battV, wh40.rssi, age);
  gfxText(0, 63, l);
}

// ---------------------------------------------------------------------------
static void drawWH57() {
  header("WH57 fulmini");
  char l[40], age[8];
  if (!wh57.valid) { gfxText(0, 36, "Nessun dato"); return; }

  snprintf(l, sizeof(l), "%lu", (unsigned long)wh57.strikesTotal);
  gfxTextBig(0, 40, l);

  if (wh57.distanceKm != 63) snprintf(l, sizeof(l), "~%u km", wh57.distanceKm);
  else                       snprintf(l, sizeof(l), "dist --");
  gfxText(GFX_W - gfxTextWidth(l), 40, l);

  const char *st = "ok";
  if      (wh57.state == 8) st = "FULMINE";
  else if (wh57.state == 4) st = "rumore";
  else if (wh57.state == 1) st = "interf.";
  else if (wh57.state == 0) st = "avvio";
  snprintf(l, sizeof(l), "stato: %s  batt %u/3", st, wh57.battLevel);
  gfxText(0, 53, l);

  ageStr(age, sizeof(age), wh57.lastSeen, true);
  snprintf(l, sizeof(l), "RSSI %.0f dBm  %s fa", wh57.rssi, age);
  gfxText(0, 63, l);
}

// ---------------------------------------------------------------------------
// Grafici: area 0..GFX_W x 16..63. mode: 0=temp, 1=umidita', 2=pioggia (delta)
// ---------------------------------------------------------------------------
static void drawGraph(int mode) {
  const char *titles[] = { "Grafico temp 24h", "Grafico umid 24h", "Grafico pioggia 24h" };
  header(titles[mode]);

  const int gx0 = 14, gy0 = 16, gx1 = GFX_W - 1, gy1 = 60;
  uint16_t n = history.count();
  if (n < 2) { gfxText(gx0, 38, "Dati insufficienti"); return; }

  // estrai serie
  float vmin = 1e9, vmax = -1e9;
  static float vals[HIST_MAX];
  int32_t prevRain = -1;
  uint16_t m = 0;
  for (uint16_t i = 0; i < n; i++) {
    const HistSample &s = history.get(i);
    float v;
    bool ok = false;
    if (mode == 0 && s.t10 != INT16_MIN) { v = s.t10 * 0.1f; ok = true; }
    if (mode == 1 && s.rh >= 0)          { v = s.rh; ok = true; }
    if (mode == 2) {
      if (s.rain10 >= 0) {
        int32_t d = (prevRain >= 0) ? (s.rain10 - prevRain) : 0;
        if (d < 0) d = s.rain10;          // reset contatore
        prevRain = s.rain10;
        v = d * 0.1f; ok = true;
      }
    }
    vals[m] = ok ? v : NAN;
    if (ok) { if (v < vmin) vmin = v; if (v > vmax) vmax = v; }
    m++;
  }
  if (vmin > vmax) { gfxText(gx0, 38, "Dati insufficienti"); return; }
  if (vmax - vmin < 1.0f) { vmax = vmin + 1.0f; }   // evita divisione per ~0

  // assi
  gfxLine(gx0, gy0, gx0, gy1);
  gfxLine(gx0, gy1, gx1, gy1);
  char lab[12];
  snprintf(lab, sizeof(lab), "%.0f", vmax);  gfxText(0, gy0 + 8, lab);
  snprintf(lab, sizeof(lab), "%.0f", vmin);  gfxText(0, gy1, lab);

  // serie
  int px = -1, py = -1;
  for (uint16_t i = 0; i < m; i++) {
    if (isnan(vals[i])) { px = -1; continue; }
    int x = gx0 + 1 + (int)((int32_t)(gx1 - gx0 - 2) * i / (m > 1 ? m - 1 : 1));
    int y = gy1 - 1 - (int)((vals[i] - vmin) / (vmax - vmin) * (gy1 - gy0 - 2));
    if (mode == 2) {
      gfxFillRect(x, y, 2, gy1 - y);                // barre per la pioggia
    } else {
      if (px >= 0) gfxLine(px, py, x, y); else gfxPixel(x, y);
      px = x; py = y;
    }
  }
}

// ---------------------------------------------------------------------------
static void drawMesh() {
  header("Meshtastic v" FW_VERSION);
  char l[44];
#if MESH_ENABLED
  snprintf(l, sizeof(l), "%.4s !%08lx", MESH_SHORT_NAME, (unsigned long)meshNodeId());
  gfxText(0, 25, l);
  snprintf(l, sizeof(l), "%s %.3fMHz", MESH_CHANNEL_NAME, (double)MESH_FREQ_MHZ);
  gfxText(0, 37, l);
  snprintf(l, sizeof(l), "SF%d BW%d %ddBm hop%d", MESH_SF, (int)MESH_BW_KHZ,
           MESH_TX_POWER_DBM, MESH_HOP_LIMIT);
  gfxText(0, 48, l);
  int32_t left = g_nextMeshMs ? (int32_t)(g_nextMeshMs - millis()) / 1000 : -1;
  if (left < 0) left = 0;
  snprintf(l, sizeof(l), "inviati %lu  next %ld:%02ld",
           (unsigned long)meshPacketsSent(), (long)(left / 60), (long)(left % 60));
  gfxText(0, 60, l);
#else
  gfxText(0, 30, "Disabilitato");
  gfxText(0, 44, "(vedi settings.ini)");
#endif
}

// ---------------------------------------------------------------------------
// SCR_TIME — solo orologio e data
//
//  TS_CONFIRMED:
//    y=10  "Orario Mesh"                    [OK]
//          ─────────────────────────────────────
//    y=30  "14:23:07"  (font grande)
//    y=46  "Ven 13 Giu 2025"
//
//  TS_UNCONFIRMED:
//    y=30  "14:23:07"
//    y=46  "conf 1/3  -42s"
//
//  TS_WAITING:
//    y=36  "04:37"  (conto alla rovescia, font grande)
//    y=54  "In attesa timestamp..."
//
//  TS_TIMEOUT:
//    y=35  "Nessun segnale"
//    y=50  "mesh nella rete"
// ---------------------------------------------------------------------------
static void drawTime() {
  TimeSyncStatus ts = timeSyncGetStatus();

  const char *stateTag =
      (ts.state == TS_CONFIRMED)   ? "OK" :
      (ts.state == TS_UNCONFIRMED) ? "?"  :
      (ts.state == TS_TIMEOUT)     ? "NO" : "RX";

  gfxText(0, 10, "Orario Mesh");
  gfxText(GFX_W - gfxTextWidth(stateTag), 10, stateTag);
  gfxLine(0, 12, GFX_W, 12);

  char l[44];

  if (ts.state == TS_CONFIRMED || ts.state == TS_UNCONFIRMED) {
    time_t now = time(nullptr);
    struct tm lt = *localtime(&now);

    snprintf(l, sizeof(l), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    gfxTextBig(4, 40, l);

    if (ts.state == TS_CONFIRMED) {
      static const char *GG[] = {"Dom","Lun","Mar","Mer","Gio","Ven","Sab"};
      static const char *MM[] = {"Gen","Feb","Mar","Apr","Mag","Giu",
                                  "Lug","Ago","Set","Ott","Nov","Dic"};
      snprintf(l, sizeof(l), "%s %d %s %d",
               GG[lt.tm_wday], lt.tm_mday, MM[lt.tm_mon], lt.tm_year + 1900);
    } else {
      snprintf(l, sizeof(l), "conf %d/%d  -%ds",
               ts.confirms, TSYNC_CONFIRM_MIN, ts.secsLeft);
    }
    gfxText(0, 56, l);

  } else if (ts.state == TS_WAITING) {
    snprintf(l, sizeof(l), "%d:%02d", ts.secsLeft / 60, ts.secsLeft % 60);
    gfxTextBig(4, 36, l);
    gfxText(0, 54, "In attesa timestamp...");

  } else {
    gfxText(4, 35, "Nessun segnale");
    gfxText(4, 50, "mesh nella rete");
  }
}

// ---------------------------------------------------------------------------
// SCR_ASTRO — effemeridi: alba, tramonto, fase lunare
//
//  Con orologio sincronizzato:
//    y=10  "Effemeridi"
//          ─────────────────────────────────────
//    y=26  "Alba     05:42"
//    y=38  "Tramonto 20:44"
//    y=51  "Gibbosa calante"
//    y=63  "72% illuminata"
//
//  Senza orologio:
//    y=33  "Orario non"
//    y=47  "disponibile"
// ---------------------------------------------------------------------------
static void drawAstro() {
  gfxText(0, 10, "Effemeridi");
  gfxLine(0, 12, GFX_W, 12);

  char l[44];

  if (!timeSyncValid()) {
    gfxText(0, 33, "Orario non");
    gfxText(0, 47, "disponibile");
    return;
  }

  SunTimes sun;
  if (astroGetSunTimes(sun)) {
    snprintf(l, sizeof(l), "Alba     %02d:%02d", sun.riseH, sun.riseM);
    gfxText(0, 26, l);
    snprintf(l, sizeof(l), "Tramonto %02d:%02d", sun.setH, sun.setM);
    gfxText(0, 38, l);
  } else {
    gfxText(0, 32, "Dati non disponibili");
  }

  gfxText(0, 51, astroMoonPhaseName());

  int illum = (int)(astroMoonIllum() * 100.0f + 0.5f);
  snprintf(l, sizeof(l), "%d%% illuminata", illum);
  gfxText(0, 63, l);
}

// ---------------------------------------------------------------------------
void screensDraw() {
  gfxClear();
  switch (curScreen) {
    case SCR_OVERVIEW:    drawOverview();     break;
    case SCR_WH32:        drawWH32();         break;
    case SCR_WH40:        drawWH40();         break;
    case SCR_WH57:        drawWH57();         break;
    case SCR_GRAPH_T:     drawGraph(0);       break;
    case SCR_GRAPH_RH:    drawGraph(1);       break;
    case SCR_GRAPH_RAIN:  drawGraph(2);       break;
    case SCR_MESH:        drawMesh();         break;
    case SCR_TIME:        drawTime();         break;
    case SCR_ASTRO:       drawAstro();        break;
    default:              drawOverview();     break;
  }
  gfxFlush();
}
