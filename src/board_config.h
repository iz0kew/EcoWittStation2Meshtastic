// ============================================================================
// board_config.h — definizione pin per le schede supportate
//   - Heltec WiFi LoRa 32 V3  (ESP32-S3 + SX1262 + OLED SSD1306)
//   - Heltec WiFi LoRa 32 V4  (pin-compatibile con la V3, PA 28 dBm)
//   - Heltec Wireless Tracker (ESP32-S3 + SX1262 + TFT ST7735 160x80)
// ============================================================================
#pragma once

#if defined(BOARD_HELTEC_V3) || defined(BOARD_HELTEC_V4)

  #define HAS_OLED 1
  #if defined(BOARD_HELTEC_V4)
    #define BOARD_NAME "Heltec V4"
  #else
    #define BOARD_NAME "Heltec V3"
  #endif

  // SX1262
  #define PIN_LORA_NSS   8
  #define PIN_LORA_SCK   9
  #define PIN_LORA_MOSI  10
  #define PIN_LORA_MISO  11
  #define PIN_LORA_RST   12
  #define PIN_LORA_BUSY  13
  #define PIN_LORA_DIO1  14

  // OLED SSD1306 (I2C)
  #define PIN_OLED_SDA   17
  #define PIN_OLED_SCL   18
  #define PIN_OLED_RST   21

  // Vext: alimenta l'OLED, attivo BASSO
  #define PIN_VEXT       36
  #define VEXT_ON_LEVEL  LOW

  #define PIN_BUTTON     0   // tasto PRG

#elif defined(BOARD_WIRELESS_TRACKER)

  #define HAS_TFT 1
  #define BOARD_NAME "Wireless Tracker"

  // SX1262 (stessi pin della V3)
  #define PIN_LORA_NSS   8
  #define PIN_LORA_SCK   9
  #define PIN_LORA_MOSI  10
  #define PIN_LORA_MISO  11
  #define PIN_LORA_RST   12
  #define PIN_LORA_BUSY  13
  #define PIN_LORA_DIO1  14

  // TFT ST7735S 160x80 (SPI dedicata)
  #define PIN_TFT_CS     38
  #define PIN_TFT_DC     40
  #define PIN_TFT_RST    39
  #define PIN_TFT_SCK    41
  #define PIN_TFT_MOSI   42
  #define PIN_TFT_BL     21  // retroilluminazione, attiva ALTA

  // Vext: alimenta TFT/GPS, attivo ALTO (opposto alla V3!)
  #define PIN_VEXT       3
  #define VEXT_ON_LEVEL  HIGH

  #define PIN_BUTTON     0

#else
  #error "Definisci BOARD_HELTEC_V3, BOARD_HELTEC_V4 o BOARD_WIRELESS_TRACKER nei build_flags"
#endif
