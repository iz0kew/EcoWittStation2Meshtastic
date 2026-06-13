#!/usr/bin/env python3
# ============================================================================
# apply_settings.py -- genera include/user_config.h a partire da settings.ini
# ============================================================================
import base64
import configparser
import os
import sys

try:
    Import("env")  # noqa: F821
    PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SETTINGS = os.path.join(PROJECT_DIR, "settings.ini")
OUTPUT   = os.path.join(PROJECT_DIR, "include", "user_config.h")

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
PRESET_NAME = {
    "shortturbo": "ShortTurbo", "shortfast": "ShortFast",
    "shortslow": "ShortSlow",   "mediumfast": "MediumFast",
    "mediumslow": "MediumSlow", "longfast": "LongFast",
    "longmoderate": "LongModerate", "longslow": "LongSlow",
}

MESHTASTIC_DEFAULT_PSK = bytes([
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
])


def die(msg):
    sys.stderr.write("\n*** settings.ini: " + msg + "\n\n")
    sys.exit(1)


def parse_channel_key(b64_value, field_name):
    """Decodifica chiave base64 -> (bytes, size).
    Accetta: AQ== (default pubblica, 16 b), 16 byte (AES-128), 32 byte (AES-256).
    """
    try:
        raw = base64.b64decode(b64_value)
    except Exception:
        die(field_name + " non e' una stringa base64 valida")
    if len(raw) == 1 and raw[0] == 0x01:
        return MESHTASTIC_DEFAULT_PSK, 16
    if len(raw) == 16:
        return raw, 16
    if len(raw) == 32:
        return raw, 32
    die(field_name + " deve essere AQ== (default), 16 byte (AES-128) o 32 byte (AES-256)"
        " -- ricevuti " + str(len(raw)) + " byte")


def get(cfg, sect, key, default=None):
    try:
        return cfg.get(sect, key).strip()
    except (configparser.NoSectionError, configparser.NoOptionError):
        if default is None:
            die("manca '" + key + "' nella sezione [" + sect + "]")
        return default


def esc_c(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def hex_arr(b):
    return ", ".join("0x{:02x}".format(x) for x in b)


def main():
    if not os.path.isfile(SETTINGS):
        die("file non trovato: " + SETTINGS)

    cfg = configparser.ConfigParser(inline_comment_prefixes=("#", ";"))
    cfg.read(SETTINGS, encoding="utf-8")

    # [meshtastic]
    enabled    = get(cfg, "meshtastic", "enabled", "true").lower() in ("1","true","yes","si")
    freq       = float(get(cfg, "meshtastic", "frequency_mhz", "869.525"))
    preset_raw = get(cfg, "meshtastic", "preset", "MediumFast")
    preset_key = preset_raw.replace(" ","").replace("_","").lower()
    if preset_key not in PRESETS:
        die("preset '" + preset_raw + "' sconosciuto. Validi: " + ", ".join(PRESET_NAME.values()))
    bw, sf, cr = PRESETS[preset_key]
    chan_name  = PRESET_NAME[preset_key]

    interval = int(get(cfg, "meshtastic", "send_interval_min", "10"))
    if interval < 1:
        die("send_interval_min deve essere >= 1")

    text_interval = int(get(cfg, "meshtastic", "text_interval_min", "0"))
    if text_interval < 0:
        die("text_interval_min non puo' essere negativo (0 = disabilitato)")

    short_name      = get(cfg, "meshtastic", "short_name", "WX")
    short_name_auto = (short_name == "")
    if not short_name_auto and len(short_name) > 4:
        print("[apply_settings] short_name '" + short_name + "' troncato a 4 caratteri")
        short_name = short_name[:4]
    long_name = get(cfg, "meshtastic", "long_name", "Stazione Meteo")[:39]

    txpwr = int(get(cfg, "meshtastic", "tx_power_dbm", "14"))
    if not -9 <= txpwr <= 22:
        die("tx_power_dbm fuori range (-9..22)")

    hop_limit = int(get(cfg, "meshtastic", "hop_limit", "3"))
    if not 0 <= hop_limit <= 7:
        die("hop_limit deve essere tra 0 e 7")

    ok_to_mqtt = get(cfg, "meshtastic", "ok_to_mqtt", "true").lower() in ("1","true","yes","si")

    chan_name_override = get(cfg, "meshtastic", "channel_name", "").strip()
    if chan_name_override:
        if len(chan_name_override) > 39:
            die("channel_name troppo lungo (max 39 caratteri)")
        chan_name = chan_name_override

    channel_key_b64 = get(cfg, "meshtastic", "channel_key", "AQ==").strip()
    psk_bytes, psk_size = parse_channel_key(channel_key_b64, "channel_key")

    text_chan_name    = get(cfg, "meshtastic", "text_channel_name", "").strip()
    text_chan_key_b64 = get(cfg, "meshtastic", "text_channel_key",  "").strip()
    text_chan_enabled = bool(text_chan_name or text_chan_key_b64)
    if text_chan_enabled:
        if not text_chan_name:
            die("text_channel_name vuoto ma text_channel_key e' valorizzato")
        if len(text_chan_name) > 39:
            die("text_channel_name troppo lungo (max 39 caratteri)")
        if not text_chan_key_b64:
            die("text_channel_key vuoto ma text_channel_name e' valorizzato")
        text_psk_bytes, text_psk_size = parse_channel_key(text_chan_key_b64, "text_channel_key")
    else:
        text_chan_name  = chan_name
        text_psk_bytes = psk_bytes
        text_psk_size  = psk_size

    lat = float(get(cfg, "meshtastic", "latitude",   "0") or "0")
    lon = float(get(cfg, "meshtastic", "longitude",  "0") or "0")
    alt = int(float(get(cfg, "meshtastic", "altitude_m", "0") or "0"))
    pos_enabled = (lat != 0.0 or lon != 0.0)
    if pos_enabled:
        if not -90  <= lat <= 90:  die("latitude fuori range")
        if not -180 <= lon <= 180: die("longitude fuori range")
        if not -450 <= alt <= 9000: die("altitude_m non plausibile")
    lat_i = int(round(lat * 1e7))
    lon_i = int(round(lon * 1e7))

    lightning           = get(cfg, "meshtastic", "lightning_text",     "true").lower() in ("1","true","yes","si")
    lightning_window    = int(get(cfg, "meshtastic", "lightning_window_min", "5"))
    if lightning_window < 1: die("lightning_window_min deve essere >= 1")
    lightning_threshold = float(get(cfg, "meshtastic", "lightning_threshold", "0.5"))
    if lightning_threshold <= 0: die("lightning_threshold deve essere > 0")

    if enabled and not (869.4 <= freq <= 869.65) and 868.0 <= freq <= 870.0:
        print("[apply_settings] ATTENZIONE: " + str(freq) + " MHz fuori dalla sotto-banda"
              " 869.4-869.65 (duty cycle 10%)")

    # [ricezione]
    rx_freq = float(get(cfg, "ricezione", "freq_mhz",       "868.35"))
    rx_br   = float(get(cfg, "ricezione", "bitrate_kbps",   "17.24"))
    rx_dev  = float(get(cfg, "ricezione", "freq_dev_khz",   "60.0"))
    rx_bw   = float(get(cfg, "ricezione", "rx_bw_khz",      "234.3"))

    # [storico]
    sample_min = int(get(cfg, "storico", "sample_interval_min", "10"))
    if sample_min < 5:
        print("[apply_settings] sample_interval_min < 5: buffer < 24h")
        sample_min = max(1, sample_min)

    # --- Costruisci user_config.h riga per riga (evita f-string multi-riga) ---
    FW_VERSION = "1.2.0"

    L = []
    def w(s=""):
        L.append(s)

    w("// ============================================================================")
    w("// user_config.h -- GENERATO AUTOMATICAMENTE da tools/apply_settings.py")
    w("// NON modificare a mano: edita settings.ini e ricompila.")
    w("// ============================================================================")
    w("#pragma once")
    w()
    w("// --- Firmware ---")
    w('#define FW_VERSION             "' + FW_VERSION + '"')
    w()
    w("// --- Meshtastic (canale principale: telemetria, NodeInfo, posizione) ---")
    w("#define MESH_ENABLED           " + str(1 if enabled else 0))
    w("#define MESH_FREQ_MHZ          " + str(freq) + "f")
    w("#define MESH_BW_KHZ            " + str(bw)   + "f")
    w("#define MESH_SF                " + str(sf))
    w("#define MESH_CR                " + str(cr))
    w('#define MESH_CHANNEL_NAME      "' + esc_c(chan_name) + '"')
    w("#define MESH_CHANNEL_KEY       {" + hex_arr(psk_bytes) + "}")
    w("#define MESH_CHANNEL_KEY_SIZE  " + str(psk_size))
    w("#define MESH_SEND_INTERVAL_MIN " + str(interval))
    w("#define MESH_TEXT_INTERVAL_MIN " + str(text_interval))
    w('#define MESH_SHORT_NAME        "' + esc_c(short_name) + '"')
    w("#define MESH_SHORT_NAME_AUTO   " + str(1 if short_name_auto else 0))
    w('#define MESH_LONG_NAME         "' + esc_c(long_name) + '"')
    w("#define MESH_TX_POWER_DBM      " + str(txpwr))
    w("#define MESH_HOP_LIMIT         " + str(hop_limit))
    w("#define MESH_OK_TO_MQTT        " + str(1 if ok_to_mqtt else 0))
    w("#define MESH_LIGHTNING_TEXT    " + str(1 if lightning else 0))
    w("#define MESH_LIGHTNING_WINDOW_MIN " + str(lightning_window))
    w("#define MESH_LIGHTNING_THRESHOLD  " + str(lightning_threshold) + "f")
    w()
    w("// --- Secondo canale (testi meteo + avvisi fulmini) ---")
    w("// Se MESH_TEXT_CHANNEL_ENABLED==0, testi e fulmini usano il canale principale.")
    w("#define MESH_TEXT_CHANNEL_ENABLED  " + str(1 if text_chan_enabled else 0))
    w('#define MESH_TEXT_CHANNEL_NAME     "' + esc_c(text_chan_name) + '"')
    w("#define MESH_TEXT_CHANNEL_KEY      {" + hex_arr(text_psk_bytes) + "}")
    w("#define MESH_TEXT_CHANNEL_KEY_SIZE " + str(text_psk_size))
    w()
    w("// --- Posizione fissa (interi a 1e-7 gradi, quota in m s.l.m.) ---")
    w("#define MESH_POS_ENABLED       " + str(1 if pos_enabled else 0))
    w("#define MESH_LAT_I             " + str(lat_i))
    w("#define MESH_LON_I             " + str(lon_i))
    w("#define MESH_ALT_M             " + str(alt))
    w()
    w("// --- Ricezione FSK sensori ---")
    w("#define RX_FREQ_MHZ            " + str(rx_freq) + "f")
    w("#define RX_BITRATE_KBPS        " + str(rx_br)   + "f")
    w("#define RX_FREQ_DEV_KHZ        " + str(rx_dev)  + "f")
    w("#define RX_BW_KHZ              " + str(rx_bw)   + "f")
    w()
    w("// --- Storico ---")
    w("#define HISTORY_SAMPLE_MIN     " + str(sample_min))
    w()

    hdr = "\n".join(L)

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    old = None
    if os.path.isfile(OUTPUT):
        with open(OUTPUT, "r", encoding="utf-8") as fh:
            old = fh.read()
    if old != hdr:
        with open(OUTPUT, "w", encoding="utf-8") as fh:
            fh.write(hdr)
        print("[apply_settings] generato " + OUTPUT)
        key_lbl = "default (AQ==)" if psk_bytes == MESHTASTIC_DEFAULT_PSK \
                  else "custom AES-" + str(psk_size * 8)
        print("[apply_settings] preset {}: BW {} kHz, SF{}, CR 4/{}, {} MHz, "
              "invio ogni {} min, hop {}".format(preset_raw, bw, sf, cr, freq, interval, hop_limit))
        print("[apply_settings] canale principale '{}', key={}".format(chan_name, key_lbl))
        if text_chan_enabled:
            txt_lbl = "default (AQ==)" if text_psk_bytes == MESHTASTIC_DEFAULT_PSK \
                      else "custom AES-" + str(text_psk_size * 8)
            print("[apply_settings] canale testo '{}', key={}".format(text_chan_name, txt_lbl))
        else:
            print("[apply_settings] canale testo: stesso del principale")
        if pos_enabled:
            print("[apply_settings] posizione fissa: {}, {}, {} m s.l.m.".format(lat, lon, alt))
        else:
            print("[apply_settings] posizione fissa: disabilitata")
    else:
        print("[apply_settings] user_config.h gia' aggiornato")


main()
