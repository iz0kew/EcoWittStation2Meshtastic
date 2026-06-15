// ============================================================================
// user_config.h -- GENERATO AUTOMATICAMENTE da tools/apply_settings.py
// NON modificare a mano: edita settings.ini e ricompila.
// ============================================================================
#pragma once

// --- Firmware ---
#define FW_VERSION             "1.4.5"

// --- Meshtastic (canale principale: telemetria, NodeInfo, posizione) ---
#define MESH_ENABLED           1
#define MESH_FREQ_MHZ          869.525f
#define MESH_BW_KHZ            250.0f
#define MESH_SF                9
#define MESH_CR                5
#define MESH_CHANNEL_NAME      "MediumFast"
#define MESH_CHANNEL_KEY       {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01}
#define MESH_CHANNEL_KEY_SIZE  16
#define MESH_SEND_INTERVAL_MIN 10
#define MESH_TEXT_INTERVAL_MIN 120
#define MESH_SHORT_NAME        "WX00"
#define MESH_SHORT_NAME_AUTO   0
#define MESH_LONG_NAME         "Stazione Meteo"
#define MESH_TX_POWER_DBM      22
#define MESH_HOP_LIMIT         3
#define MESH_OK_TO_MQTT        1
#define MESH_LIGHTNING_TEXT    1
#define MESH_LIGHTNING_WINDOW_MIN 5
#define MESH_LIGHTNING_THRESHOLD  0.5f

// --- Secondo canale (testi meteo + avvisi fulmini) ---
// Se MESH_TEXT_CHANNEL_ENABLED==0, testi e fulmini usano il canale principale.
#define MESH_TEXT_CHANNEL_ENABLED  1
#define MESH_TEXT_CHANNEL_NAME     "METEOLAZIO"
#define MESH_TEXT_CHANNEL_KEY      {0x58, 0xfe, 0xaa, 0x1c, 0xf0, 0x84, 0x15, 0x16, 0x44, 0xf2, 0x22, 0xbc, 0x48, 0x74, 0x29, 0x67, 0x52, 0xbd, 0xd7, 0x8e, 0x46, 0xea, 0x37, 0x6e, 0xdd, 0x89, 0xcf, 0xf9, 0xd3, 0x12, 0x3a, 0x0d}
#define MESH_TEXT_CHANNEL_KEY_SIZE 32

// --- Posizione fissa (interi a 1e-7 gradi, quota in m s.l.m.) ---
#define MESH_POS_ENABLED       1
#define MESH_LAT_I             418603000
#define MESH_LON_I             130337000
#define MESH_ALT_M             571

// --- Fuso orario (stringa TZ POSIX per setenv/tzset su ESP32) ---
// Derivato automaticamente dalle coordinate; override con timezone=... in settings.ini
#define MESH_TIMEZONE          "CET-1CEST,M3.5.0,M10.5.0/3"

// --- Ricezione FSK sensori ---
#define RX_FREQ_MHZ            868.35f
#define RX_BITRATE_KBPS        17.24f
#define RX_FREQ_DEV_KHZ        60.0f
#define RX_BW_KHZ              234.3f

// --- Storico ---
#define HISTORY_SAMPLE_MIN     10
