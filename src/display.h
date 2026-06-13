// ============================================================================
// display.h — astrazione display con spazio logico 128x64
//   backend OLED (SSD1306, Heltec V3/V4) in display_oled.cpp
//   backend TFT  (ST7735 160x80, Wireless Tracker) in display_tft.cpp
// Le schermate sono disegnate in screens.cpp usando queste primitive.
// ============================================================================
#pragma once
#include <Arduino.h>

// --- primitive implementate dal backend ---
void gfxInit();
void gfxClear();
void gfxFlush();
void gfxText(int x, int y, const char *s);        // font piccolo ~6x10, y = baseline
void gfxTextBig(int x, int y, const char *s);     // font grande per il valore principale
int  gfxTextWidth(const char *s);                 // larghezza testo piccolo
void gfxLine(int x0, int y0, int x1, int y1);
void gfxFillRect(int x, int y, int w, int h);
void gfxPixel(int x, int y);

#define GFX_W 128
#define GFX_H 64

// --- gestione schermate (screens.cpp) ---
enum Screen {
  SCR_OVERVIEW = 0,
  SCR_WH32,
  SCR_WH40,
  SCR_WH57,
  SCR_GRAPH_T,
  SCR_GRAPH_RH,
  SCR_GRAPH_RAIN,
  SCR_MESH,
  SCR_TIME,        // orario sincronizzato dalla rete Meshtastic
  SCR_ASTRO,       // effemeridi: alba, tramonto, fase lunare
  SCR_COUNT
};

void screensDraw();
void screensNext();
void screensSet(int scr);   // imposta direttamente la schermata senza ciclare
