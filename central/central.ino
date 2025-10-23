// ================= Central.ino (ESP32) =================
// Main controller for the Minecraft Bop-It game
// Manages WiFi AP, WebSocket connections, and game logic coordination

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

// ======================== CONFIGURATION ========================

// Hardware pins
#ifndef WIFI_STATUS_LED
#define WIFI_STATUS_LED 2
#endif

// WiFi Access Point configuration
const char* AP_SSID = "BlockParty";
const char* AP_PASS = "craft123";
const uint8_t AP_CHANNEL = 6;
const uint8_t AP_MAX_CONNECTIONS = 8;

// Timing constants (milliseconds)
const uint32_t SYNC_INTERVAL_MS = 1000;  // Time sync broadcast interval
const uint32_t PRUNE_INTERVAL_MS = 2000; // Player connection check interval
const uint32_t PLAYER_TIMEOUT_MS = 5000; // Player disconnect timeout
const uint32_t ROUND_DELAY_MS = 800;     // Delay between rounds
const uint32_t DEADLINE_GRACE_MS = 20;   // Grace period after round deadline

// Other constants
const uint16_t HTTP_STATUS_OK = 200;
const uint16_t HTTP_STATUS_NOT_FOUND = 404;
const uint32_t BAUD_RATE = 115200; // Serial communication baud rate
const uint32_t SERIAL_INIT_DELAY_MS = 100; // Allow serial to initialize
const uint32_t STATUS_LIGHT_DELAY_MS = 100; // Default delay for status LED blink

// ======================== GLOBAL INSTANCES ========================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Game* game = nullptr;

// ======================== WEBSOCKET MESSAGE HANDLERS ========================

void handleBlockHello(AsyncWebSocketClient* client, JSONVar& doc) {
  if (!client || !game) {
    return;
  }
  
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  ClientMeta* meta = game->getClient(client->id());
  if (!meta) {
    return;
  }
  
  meta->role = "block";
  meta->blockId = blockId;

  Player& player = game->addPlayer(blockId);
  player.setConnected(true);
  player.setLastSeenMs(millis());
}

void handleWebHello(AsyncWebSocketClient* client, JSONVar& doc) {
  if (!client || !game) {
    return;
  }
  
  ClientMeta* meta = game->getClient(client->id());
  if (!meta) {
    return;
  }
  
  meta->role = "web";
  meta->blockId = ""; // Web clients don't have block IDs
  
  Serial.printf("Web client connected: %u\n", client->id());
  
  // Send current game state to the newly connected web client
  game->broadcastStateToWeb(client->id());
}

void handleBlockStatus(JSONVar& doc) {
  if (!game) {
    return;
  }
  
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  Player* player = game->getPlayer(blockId);
  if (!player) {
    return;
  }
  
  player->setConnected(true);
  player->setLastSeenMs(millis());
}

void handleBlockResult(JSONVar& doc) {
  if (!game) {
    return;
  }
  
  // Only accept results during active game phases
  Phase currentPhase = game->getPhase();
  if (currentPhase != Phase::RUNNING && currentPhase != Phase::WAITING_NEXT_ROUND) {
    return;
  }

  // Validate round number
  int round = doc.hasOwnProperty("round") ? (int)doc["round"] : -999;
  if (round != game->getRound()) {
    return;
  }
  
  // Validate block ID
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  Player* player = game->getPlayer(blockId);
  if (!player) {
    return;
  }
  
  if (!player->isInGame()) {
    return;
  }

  // Process the result
  bool actionDone = doc.hasOwnProperty("actionDone") ? (bool)doc["actionDone"] : false;
  player->setReported(true);
  player->setSuccess(actionDone);
  
  if (actionDone) {
    player->incrementScore();
  }
}

void handleAdmin(AsyncWebSocketClient* client, JSONVar& doc) {
  if (!client || !game) {
    return;
  }
  
  // Verify client authentication
  ClientMeta* meta = game->getClient(client->id());
  if (!meta || meta->role != "web") {
    return;
  }
  
  String action = doc.hasOwnProperty("action") ? (const char*)doc["action"] : "";
  if (action.isEmpty()) {
    return;
  }

  // Process admin commands
  if (action == "start") {
    int round0Ms = doc.hasOwnProperty("round0Ms") ? (int)doc["round0Ms"] : 2500;
    int decayMs = doc.hasOwnProperty("decayMs") ? (int)doc["decayMs"] : 150;
    int minMs = doc.hasOwnProperty("minMs") ? (int)doc["minMs"] : 800;
    
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
    
    if (blockId.isEmpty() || name.isEmpty()) {
      return;
    }
    
    game->renamePlayer(blockId, name);
    
  } else {
    // Unknown admin action
  }
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {

  if (!game) {
    return;
  }
  
  switch (type) {
    case WS_EVT_CONNECT:
      game->addClient(client->id());
      // Initial state will be sent after hello message
      break;
      
    case WS_EVT_DISCONNECT:
    {
      // Handle block disconnection
      ClientMeta* meta = game->getClient(client->id());
      if (meta && meta->role == "block" && !meta->blockId.isEmpty()) {
        Player* player = game->getPlayer(meta->blockId);
        if (player) {
          player->setConnected(false);
        }
      }
      
      game->removeClient(client->id());
      break;
    }
      
    case WS_EVT_DATA:
    {
      // Parse and validate JSON message
      if (len == 0 || !data) {
        return;
      }
      
      String jsonString = String((char*)data).substring(0, len);
      JSONVar doc = JSON.parse(jsonString);
      
      if (JSON.typeof(doc) == "undefined") {
        return;
      }

      // Route message based on type
      String msgType = doc.hasOwnProperty("type") ? (const char*)doc["type"] : "";
      if (msgType.isEmpty()) {
        return;
      }
      
      if (msgType == "hello") {
        handleBlockHello(client, doc);
      } else if (msgType == "web-hello") {
        handleWebHello(client, doc);
      } else if (msgType == "status") {
        handleBlockStatus(doc);
      } else if (msgType == "result") {
        handleBlockResult(doc);
      } else if (msgType == "admin") {
        handleAdmin(client, doc);
      } else {
        game->broadcastStateToWeb();
      }
      break;
    }
      
    case WS_EVT_ERROR:
      break;
      
    case WS_EVT_PONG:
      break;
      
    default:
      // Unknown WebSocket event type
      break;
  }
}

// ======================== HTTP SERVER SETUP ========================

void setupHttp() {  
  // Serve main web interface
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(HTTP_STATUS_OK, "text/html", INDEX_HTML);
  });

  // Add basic error handling
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_NOT_FOUND, "text/plain", "Not Found");
  });
  
  // Configure WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  // Start server
  server.begin();
}

// ======================== HELPER FUNCTIONS ========================

bool setupWiFiAP() {  
  WiFi.mode(WIFI_AP);
  bool success = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
  
  if (success) {
    // Turn on status LED
    digitalWrite(WIFI_STATUS_LED, HIGH);
  } else {
    digitalWrite(WIFI_STATUS_LED, LOW);
  }
  
  return success;
}

void broadcastTimeSync() {
  JSONVar syncMsg;
  syncMsg["type"] = "sync";
  syncMsg["serverTimeMs"] = (unsigned long)millis();
  
  String message = JSON.stringify(syncMsg);
  ws.textAll(message);
}

void pruneDisconnectedPlayers() {
  uint32_t currentTime = millis();
  
  for (const auto& player : game->getPlayers()) {
    if (player->isConnected() && 
        (currentTime - player->getLastSeenMs() > PLAYER_TIMEOUT_MS)) {
      Serial.printf("Player timeout: %s (last seen %lu ms ago)\n", 
                    player->getBlockId().c_str(), 
                    currentTime - player->getLastSeenMs());
      player->setConnected(false);
    }
  }
}

void processRoundTiming() {
  Phase currentPhase = game->getPhase();
  uint32_t currentTime = millis();
  
  // Check for round end during active gameplay
  if (currentPhase == Phase::RUNNING) {
    if (currentTime > game->getDeadlineMs() + DEADLINE_GRACE_MS) {
      game->endRound();
      
      // Schedule next round
      game->setRoundStartMs(currentTime + ROUND_DELAY_MS);
      game->setPhase(Phase::WAITING_NEXT_ROUND);
    }
  }
  
  // Handle scheduled next round start
  else if (currentPhase == Phase::WAITING_NEXT_ROUND) {
    if (currentTime >= game->getRoundStartMs()) {
      if (game->isPauseQueued()) {
        game->setPauseQueued(false);
        game->setPhase(Phase::PAUSED);
      } else {
        game->setPhase(Phase::RUNNING);
        game->nextRound();
      }
    }
  }
}

// ======================== MAIN SETUP & LOOP ========================

// Timing variables for main loop intervals
uint32_t lastSyncMs = 0;
uint32_t lastPruneMs = 0;

void setup() {
  // Initialize status LED (will be turned on when WiFi AP is ready)
  pinMode(WIFI_STATUS_LED, OUTPUT);
  digitalWrite(WIFI_STATUS_LED, LOW);

  // Initialize serial communication
  Serial.begin(BAUD_RATE);
  delay(SERIAL_INIT_DELAY_MS); // Allow serial to initialize

  // Initialize game instance
  game = new Game(&ws);
  if (!game) {
    Serial.println("FATAL ERROR: Failed to create game instance");
    while (true) {
      digitalWrite(WIFI_STATUS_LED, HIGH);
      delay(STATUS_LIGHT_DELAY_MS);
      digitalWrite(WIFI_STATUS_LED, LOW);
      delay(STATUS_LIGHT_DELAY_MS);
    }
  }

  // Setup WiFi Access Point
  if (!setupWiFiAP()) {
    while (true) {
      digitalWrite(WIFI_STATUS_LED, HIGH);
      delay(STATUS_LIGHT_DELAY_MS);
      digitalWrite(WIFI_STATUS_LED, LOW);
      delay(STATUS_LIGHT_DELAY_MS);
    }
  }

  // Setup HTTP server and WebSocket
  setupHttp();
}

void loop() {
  uint32_t currentTime = millis();
  
  // 1) Broadcast time synchronization to all clients
  if (currentTime - lastSyncMs >= SYNC_INTERVAL_MS) {
    lastSyncMs = currentTime;
    broadcastTimeSync();
  }

  // 2) Check for disconnected players and update their status
  currentTime = millis();
  if (currentTime - lastPruneMs >= PRUNE_INTERVAL_MS) {
    lastPruneMs = currentTime;
    pruneDisconnectedPlayers();
  }

  // 3) Handle game round timing (only check frequently during active phases)
  Phase currentPhase = game->getPhase();
  if ((currentPhase == Phase::RUNNING || currentPhase == Phase::WAITING_NEXT_ROUND)) {
    processRoundTiming();
  }

  // 4) Clean up disconnected WebSocket clients
  ws.cleanupClients();
  
  // Small delay to prevent overwhelming the system
  delay(1);
}
