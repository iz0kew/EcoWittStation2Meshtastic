// ============================================================================
// meshtastic_tx.h — invio pacchetti sulla rete Meshtastic
// Implementa il protocollo radio Meshtastic "a mano":
//   header 16 byte + payload protobuf (Data) cifrato AES128-CTR
// Riferimenti: meshtastic/firmware src/mesh/{RadioInterface.h, CryptoEngine.cpp,
//   Channels.cpp}, meshtastic/protobufs {mesh,telemetry,portnums}.proto
// ============================================================================
#pragma once
#include <Arduino.h>

void     meshInit();
uint32_t meshNodeId();
uint32_t meshPacketsSent();

// Telemetria ambientale (EnvironmentMetrics): appare nei grafici dell'app
bool meshSendTelemetry(bool haveTH, float tempC, float rh,
                       bool haveRain, float rain1h, float rain24h);

// NodeInfo: presenta il nodo (short/long name) agli altri nodi
bool meshSendNodeInfo();

// Posizione fissa (da settings.ini): il nodo appare sulla mappa dell'app
bool meshSendPosition();

// Messaggio di testo sul canale (usato per gli avvisi fulmini)
bool meshSendText(const char *txt);
