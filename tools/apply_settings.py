#!/usr/bin/env python3
# ============================================================================
# apply_settings.py — genera include/user_config.h a partire da settings.ini
#
# Viene eseguito automaticamente da PlatformIO prima di ogni compilazione
# (extra_scripts = pre:tools/apply_settings.py), ma puo' essere lanciato
# anche a mano:   python tools/apply_settings.py
# ============================================================================
import base64
import configparser
import os
import sys

# --- individua la directory di progetto (PlatformIO o standalone) -----------
try:
    Import("env")  # noqa: F821  (definito da SCons dentro PlatformIO)
    PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SETTINGS = os.path.join(PROJECT_DIR, "settings.ini")
OUTPUT = os.path.join(PROJECT_DIR, "include", "user_config.h")

# --- tabella preset Meshtastic (banda stretta, non wideLora) ----------------
# nome -> (bw_khz, sf, cr)  — da meshtastic/firmware modemPresetToParams()
PRESETS = {
    "shortturbo":   (500.0, 7,  5),
    "shortfast":    (250.0, 7,  5),
    "shortslow":    (250.0, 8,  5),
    "mediumfast":   (250.0, 9,  5),
    "mediumslow":   (250.0, 10, 5),
    "longfast":     (250.0, 11, 5),
    "longmoderate": (125.0, 11, 8),
    "longslow":     (125.0, 12, 8),
}
# nome canonico (usato come nome del canale di default -> hash -> slot)
PRESET_NAME = {
    "shortturbo": "ShortTurbo", "shortfast": "ShortFast",
    "shortslow": "ShortSlow", "mediumfast": "MediumFast",
    "mediumslow": "MediumSlow", "longfast": "LongFast",
    "longmoderate": "LongModerate", "longslow": "LongSlow",
}

# PSK espansa del canale pubblico di default Meshtastic (AQ== -> 16 byte AES)
MESHTASTIC_DEFAULT_PSK = bytes([
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
])


def die(msg):
    sys.stderr.write(f"\n*** settings.ini: {msg}\n\n")
    sys.exit(1)


def get(cfg, sect, key, default=None):
    try:
        return cfg.get(sect, key).strip()
    except (configparser.NoSectionError, configparser.NoOptionError):
        if default is None:
            die(f"manca la voce '{key}' nella sezione [{sect}]")
        return default


def main():
    if not os.path.isfile(SETTINGS):
        die(f"file non trovato: {SETTINGS}")

    cfg = configparser.ConfigParser(inline_comment_prefixes=("#", ";"))
    cfg.read(SETTINGS, encoding="utf-8")

    # --- [meshtastic] ---
    enabled = get(cfg, "meshtastic", "enabled", "true").lower() in ("1", "true", "yes", "si")
    freq = float(get(cfg, "meshtastic", "frequency_mhz", "869.525"))
    preset_raw = get(cfg, "meshtastic", "preset", "MediumFast")
    preset_key = preset_raw.replace(" ", "").replace("_", "").lower()
    if preset_key not in PRESETS:
        die(f"preset '{preset_raw}' sconosciuto. Validi: {', '.join(PRESET_NAME.values())}")
    bw, sf, cr = PRESETS[preset_key]
    chan_name = PRESET_NAME[preset_key]

    interval = int(get(cfg, "meshtastic", "send_interval_min", "10"))
    if interval < 1:
        die("send_interval_min deve essere >= 1")

    text_interval = int(get(cfg, "meshtastic", "text_interval_min", "0"))
    if text_interval < 0:
        die("text_interval_min non puo' essere negativo (0 = disabilitato)")

    short_name = get(cfg, "meshtastic", "short_name", "WX")
    if len(short_name) > 4:
        print(f"[apply_settings] short_name '{short_name}' troncato a 4 caratteri")
        short_name = short_name[:4]
    long_name = get(cfg, "meshtastic", "long_name", "Stazione Meteo")[:39]

    txpwr = int(get(cfg, "meshtastic", "tx_power_dbm", "14"))
    if not -9 <= txpwr <= 22:
        die("tx_power_dbm fuori range per SX1262 (-9..22)")

    hop_limit = int(get(cfg, "meshtastic", "hop_limit", "3"))
    if not 0 <= hop_limit <= 7:
        die("hop_limit deve essere tra 0 e 7")

    ok_to_mqtt = get(cfg, "meshtastic", "ok_to_mqtt", "true").lower() in ("1", "true", "yes", "si")

    # channel_name: opzionale, default = nome canonico del preset
    channel_name_override = get(cfg, "meshtastic", "channel_name", "").strip()
    if channel_name_override:
        if len(channel_name_override) > 39:
            die("channel_name troppo lungo (max 39 caratteri)")
        chan_name = channel_name_override

    # channel_key: base64, default = AQ== (chiave pubblica Meshtastic)
    channel_key_b64 = get(cfg, "meshtastic", "channel_key", "AQ==").strip()
    try:
        raw_key = base64.b64decode(channel_key_b64)
    except Exception:
        die("channel_key non e' una stringa base64 valida")
    if len(raw_key) == 1 and raw_key[0] == 0x01:
        psk_bytes = MESHTASTIC_DEFAULT_PSK
    elif len(raw_key) == 16:
        psk_bytes = raw_key
    else:
        die("channel_key deve essere AQ== (default) oppure base64 di esattamente 16 byte")

    lat = float(get(cfg, "meshtastic", "latitude", "0") or "0")
    lon = float(get(cfg, "meshtastic", "longitude", "0") or "0")
    alt = int(float(get(cfg, "meshtastic", "altitude_m", "0") or "0"))
    pos_enabled = (lat != 0.0 or lon != 0.0)
    if pos_enabled:
        if not -90 <= lat <= 90:
            die("latitude fuori range (-90..90)")
        if not -180 <= lon <= 180:
            die("longitude fuori range (-180..180)")
        if not -450 <= alt <= 9000:
            die("altitude_m non plausibile (-450..9000)")
    lat_i = int(round(lat * 1e7))   # interi a 1e-7 gradi, come Meshtastic
    lon_i = int(round(lon * 1e7))

    lightning = get(cfg, "meshtastic", "lightning_text", "true").lower() in ("1", "true", "yes", "si")

    lightning_window = int(get(cfg, "meshtastic", "lightning_window_min", "5"))
    if lightning_window < 1:
        die("lightning_window_min deve essere >= 1")

    lightning_threshold = float(get(cfg, "meshtastic", "lightning_threshold", "0.5"))
    if lightning_threshold <= 0:
        die("lightning_threshold deve essere > 0")

    # sanity check banda/frequenza EU868
    if enabled and not (869.4 <= freq <= 869.65) and 868.0 <= freq <= 870.0:
        print(f"[apply_settings] ATTENZIONE: {freq} MHz e' fuori dalla sotto-banda "
              f"869.4-869.65 (duty cycle 10%). Verifica i limiti normativi.")

    # --- [ricezione] ---
    rx_freq = float(get(cfg, "ricezione", "freq_mhz", "868.35"))
    rx_br = float(get(cfg, "ricezione", "bitrate_kbps", "17.24"))
    rx_dev = float(get(cfg, "ricezione", "freq_dev_khz", "60.0"))
    rx_bw = float(get(cfg, "ricezione", "rx_bw_khz", "234.3"))

    # --- [storico] ---
    sample_min = int(get(cfg, "storico", "sample_interval_min", "10"))
    if sample_min < 5:
        print("[apply_settings] sample_interval_min < 5: il buffer coprira' meno di 24h")
        sample_min = max(1, sample_min)

    def esc(s):
        return s.replace("\\", "\\\\").replace('"', '\\"')

    psk_hex = ", ".join(f"0x{b:02x}" for b in psk_bytes)

    FW_VERSION = "1.1.1"   # aggiorna qui ad ogni release

    hdr = f"""// ============================================================================
// user_config.h — GENERATO AUTOMATICAMENTE da tools/apply_settings.py
// NON modificare a mano: edita settings.ini e ricompila.
// ============================================================================
#pragma once

// --- Firmware ---
#define FW_VERSION             "{FW_VERSION}"

// --- Meshtastic ---
#define MESH_ENABLED           {1 if enabled else 0}
#define MESH_FREQ_MHZ          {freq}f
#define MESH_BW_KHZ            {bw}f
#define MESH_SF                {sf}
#define MESH_CR                {cr}
#define MESH_CHANNEL_NAME      "{esc(chan_name)}"
#define MESH_CHANNEL_KEY       {{{psk_hex}}}
#define MESH_SEND_INTERVAL_MIN {interval}
#define MESH_TEXT_INTERVAL_MIN {text_interval}
#define MESH_SHORT_NAME        "{esc(short_name)}"
#define MESH_LONG_NAME         "{esc(long_name)}"
#define MESH_TX_POWER_DBM      {txpwr}
#define MESH_HOP_LIMIT         {hop_limit}
#define MESH_OK_TO_MQTT        {1 if ok_to_mqtt else 0}
#define MESH_LIGHTNING_TEXT    {1 if lightning else 0}
#define MESH_LIGHTNING_WINDOW_MIN {lightning_window}
#define MESH_LIGHTNING_THRESHOLD  {lightning_threshold}f

// --- Posizione fissa (interi a 1e-7 gradi, quota in m s.l.m.) ---
#define MESH_POS_ENABLED       {1 if pos_enabled else 0}
#define MESH_LAT_I             {lat_i}
#define MESH_LON_I             {lon_i}
#define MESH_ALT_M             {alt}

// --- Ricezione FSK sensori ---
#define RX_FREQ_MHZ            {rx_freq}f
#define RX_BITRATE_KBPS        {rx_br}f
#define RX_FREQ_DEV_KHZ        {rx_dev}f
#define RX_BW_KHZ              {rx_bw}f

// --- Storico ---
#define HISTORY_SAMPLE_MIN     {sample_min}
"""

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    old = None
    if os.path.isfile(OUTPUT):
        with open(OUTPUT, "r", encoding="utf-8") as f:
            old = f.read()
    if old != hdr:
        with open(OUTPUT, "w", encoding="utf-8") as f:
            f.write(hdr)
        print(f"[apply_settings] generato {OUTPUT}")
        key_label = "default (AQ==)" if psk_bytes == MESHTASTIC_DEFAULT_PSK else "custom"
        print(f"[apply_settings] preset {preset_raw}: BW {bw} kHz, SF{sf}, CR 4/{cr}, "
              f"{freq} MHz, invio ogni {interval} min, hop {hop_limit}")
        print(f"[apply_settings] canale '{chan_name}', key={key_label}")
        if pos_enabled:
            print(f"[apply_settings] posizione fissa: {lat}, {lon}, {alt} m s.l.m.")
        else:
            print("[apply_settings] posizione fissa: disabilitata")
    else:
        print("[apply_settings] user_config.h gia' aggiornato")


main()
