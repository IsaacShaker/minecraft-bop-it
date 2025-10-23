// ================= Central.ino (ESP32) =================
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <vector>
#include "Web/web_interface.h"
#include "Game/Game.h"
#include "Player/Player.h"
// Include implementations for Arduino IDE (since .cpp files in subdirs aren't auto-compiled)
#include "Game/Game.cpp"
#include "Player/Player.cpp"

// WIFI Status LED
#ifndef WIFI_STATUS_LED
#define WIFI_STATUS_LED 2
#endif

// -------- SoftAP config --------
const char* AP_SSID = "BlockParty";
const char* AP_PASS = "craft123";

// -------- HTTP + WS --------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// -------- Game Instance --------
Game* game = nullptr;

// -------- WebSocket events --------
void handleBlockHello(AsyncWebSocketClient* client, JSONVar& doc) {
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  auto *meta = game->getClient(client->id());
  if (!meta) return;
  meta->role = "block";
  meta->blockId = blockId;

  Player &p = game->addPlayer(blockId);
  p.setConnected(true);
  p.setLastSeenMs(millis());
}

void handleWebHello(AsyncWebSocketClient* client, JSONVar& doc) {
  auto *meta = game->getClient(client->id());
  if (!meta) return;
  meta->role = "web";
  meta->blockId = ""; // web clients don't have block IDs
  
  Serial.printf("Web client connected: %u\n", client->id());
  // Send current state to the newly connected web client
  /// @todo should have a method that sets the role of a client that automatically broadcasts?
  game->broadcastStateToWeb(client->id());
}

void handleBlockStatus(JSONVar& doc) {
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  Player *p = game->getPlayer(blockId);
  if (!p) return;
  p->setConnected(true);
  p->setLastSeenMs(millis());
}

void handleBlockResult(JSONVar& doc) {
  if (game->getPhase() != Phase::RUNNING && game->getPhase() != Phase::WAITING_NEXT_ROUND) return;

  int round = doc.hasOwnProperty("round") ? (int)doc["round"] : -999;
  if (round != game->getRound()) return;
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  Player *p = game->getPlayer(blockId);
  if (!p || !p->isInGame()) return;

  bool actionDone = doc.hasOwnProperty("actionDone") ? (bool)doc["actionDone"] : false;
  p->setReported(true);
  p->setSuccess(actionDone);
  if (actionDone) {
    p->incrementScore();
  }
}

void handleAdmin(AsyncWebSocketClient* client, JSONVar& doc) {
  // Verify that the client is authenticated as a web client
  auto *meta = game->getClient(client->id());
  if (!meta || meta->role != "web") {
    Serial.printf("Unauthorized admin attempt from client %u (role: %s)\n", 
                  client->id(), meta ? meta->role.c_str() : "unknown");
    return;
  }
  
  String action = doc.hasOwnProperty("action") ? (const char*)doc["action"] : "";
  if (action == "start") {
    int round0Ms = doc.hasOwnProperty("round0Ms") ? (int)doc["round0Ms"] : 2500;
    int decayMs  = doc.hasOwnProperty("decayMs") ? (int)doc["decayMs"] : 150;
    int minMs    = doc.hasOwnProperty("minMs") ? (int)doc["minMs"] : 800;
    game->startGame(round0Ms, decayMs, minMs);
  } else if (action == "pause") {
    game->pauseGame();
  } else if (action == "resume") {
    game->resumeGame();
  } else if (action == "reset") {
    game->resetGame();
  } else if (action == "rename") {
    String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
    String name = doc.hasOwnProperty("name") ? (const char*)doc["name"] : "";
    game->renamePlayer(blockId, name);
  }
}

void onWsEvent(AsyncWebSocket       * server,
               AsyncWebSocketClient * client,
               AwsEventType           type,
               void *                 arg,
               uint8_t *              data,
               size_t                 len) {
  if (type == WS_EVT_CONNECT) {
    game->addClient(client->id());
    // Send current state only to the newly connected client
    game->broadcastStateToWeb(client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    auto *meta = game->getClient(client->id());
    if (meta && meta->role == "block" && meta->blockId.length()) {
      Player *p = game->getPlayer(meta->blockId);
      if (p) p->setConnected(false);
    }
    game->removeClient(client->id());
  } else if (type == WS_EVT_DATA) {
    // Parse JSON
    String jsonString = String((char*)data).substring(0, len);
    JSONVar doc = JSON.parse(jsonString);
    if (JSON.typeof(doc) == "undefined") return;

    const char* t = doc.hasOwnProperty("type") ? (const char*)doc["type"] : "";
    if (!strcmp(t, "hello")) {
      handleBlockHello(client, doc);
    } else if (!strcmp(t, "web-hello")) {
      handleWebHello(client, doc);
    } else if (!strcmp(t, "status")) {
      handleBlockStatus(doc);
    } else if (!strcmp(t, "result")) {
      handleBlockResult(doc);
    } else if (!strcmp(t, "admin")) {
      handleAdmin(client, doc);
    } else {
      game->broadcastStateToWeb();
    }
  }
}

// -------- Setup HTTP + File System --------
void setupHttp() {
  // Serve embedded page (INDEX_HTML defined in web_interface.h)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send_P(200, "text/html", INDEX_HTML);
  });
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  
  Serial.println("HTTP server started");
  Serial.println("Web interface should be available at: http://192.168.4.1:80");
}

// -------- Setup / Loop --------
uint32_t lastSyncMs = 0;
uint32_t lastPruneMs = 0;
uint32_t lastRoundCheckMs = 0;

void setup() {
  // WiFi Status LED (HIGH = Connected)
  pinMode(WIFI_STATUS_LED, OUTPUT);
  digitalWrite(WIFI_STATUS_LED, LOW);

  //Setup Serial
  Serial.begin(115200);

  // Initialize game instance
  game = new Game(&ws);

  // Settup WiFi AP
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 8);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  if (ok) {
    digitalWrite(WIFI_STATUS_LED, HIGH); // solid ON = AP up
  }

  setupHttp();
}

void loop() {
  // 1) push time sync to blocks (~1s)
  if (millis() - lastSyncMs > 1000) {
    lastSyncMs = millis();
    JSONVar doc;
    doc["type"] = "sync";
    doc["serverTimeMs"] = (unsigned long)millis();
    String out = JSON.stringify(doc);
    ws.textAll(out);
  }

  // 2) prune disconnected players if stale
  if (millis() - lastPruneMs > 2000) {
    lastPruneMs = millis();
    for (const auto& player : game->getPlayers()) {
      if (player->isConnected() && (millis() - player->getLastSeenMs() > 5000)) {
        player->setConnected(false);
      }
    }
  }

  // 3) round timing gates (end & next)
  if (game->getPhase() == Phase::RUNNING && millis() - lastRoundCheckMs > 50) {
    lastRoundCheckMs = millis();
    if (millis() > game->getDeadlineMs() + 20) {
      game->endRound();
      // Schedule next round start time (non-blocking approach)
      game->setRoundStartMs(millis() + 800);  // 800ms delay before next round
      game->setPhase(Phase::WAITING_NEXT_ROUND);  // Temporarily pause to wait for next round
    }
  }

  // 4) Handle scheduled next round start
  if (game->getPhase() == Phase::WAITING_NEXT_ROUND && millis() >= game->getRoundStartMs()) {
    if (game->isPauseQueued()) {
        game->setPauseQueued(false);
        game->setPhase(Phase::PAUSED);
    }
    else {
      game->setPhase(Phase::RUNNING);
      game->nextRound();
    }
  }

  ws.cleanupClients();
}
