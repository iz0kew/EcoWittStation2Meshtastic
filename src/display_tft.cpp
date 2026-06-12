#include "board_config.h"
#ifdef HAS_TFT

#include "display.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// SPI dedicata del TFT (separata da quella dell'SX1262)
static SPIClass tftSPI(HSPI);
static Adafruit_ST7735 tft(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// Disegniamo su un canvas 160x80 e lo spingiamo in blocco (niente sfarfallio).
// Lo spazio logico 128x64 viene centrato con offset (16, 8).
static GFXcanvas16 canvas(160, 80);
static const int OX = 16, OY = 8;

static const uint16_t COL_BG = ST77XX_BLACK;
static const uint16_t COL_FG = ST77XX_WHITE;

void gfxInit() {
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);                 // retroilluminazione
  tftSPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.initR(INITR_MINI160x80);
  // NOTA: se i colori appaiono invertiti sul tuo pannello, decommenta:
  tft.invertDisplay(true);
  tft.setRotation(1);                             // landscape 160x80
  tft.fillScreen(COL_BG);
}

void gfxClear() { canvas.fillScreen(COL_BG); }

void gfxFlush() {
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 80);
}

// font GFX di default: 6x8, cursore in alto a sinistra -> compensiamo la baseline
void gfxText(int x, int y, const char *s) {
  canvas.setTextColor(COL_FG);
  canvas.setTextSize(1);
  canvas.setCursor(OX + x, OY + y - 8);
  canvas.print(s);
}

void gfxTextBig(int x, int y, const char *s) {
  canvas.setTextColor(COL_FG);
  canvas.setTextSize(3);                          // 18x24
  canvas.setCursor(OX + x, OY + y - 24);
  canvas.print(s);
}

int gfxTextWidth(const char *s) { return (int)strlen(s) * 6; }

void gfxLine(int x0, int y0, int x1, int y1) {
  canvas.drawLine(OX + x0, OY + y0, OX + x1, OY + y1, COL_FG);
}

void gfxFillRect(int x, int y, int w, int h) {
  if (w > 0 && h > 0) canvas.fillRect(OX + x, OY + y, w, h, COL_FG);
}

void gfxPixel(int x, int y) { canvas.drawPixel(OX + x, OY + y, COL_FG); }

#endif // HAS_TFT
