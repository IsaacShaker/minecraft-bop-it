// ================= Central.ino (ESP32) =================
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <vector>

// -------- SoftAP config --------
const char* AP_SSID = "Verizon_7DGDM4";
const char* AP_PASS = "brave-yak4-washed-up";

// -------- HTTP + WS --------
AsyncWebServer server(8080);
AsyncWebSocket ws("/ws");

// ---- Big embedded page (edit freely) ----
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Block Party</title>
  <style>
    body { font-family: system-ui, Arial, sans-serif; margin: 16px; }
    #status { padding: 6px 10px; display:inline-block; border-radius:6px; background:#eef; }
    button { margin-right:8px; }
    table { border-collapse: collapse; margin-top:12px; width:100%; }
    th, td { padding:6px 8px; border-bottom:1px solid #eee; text-align:left; }
    .badge { padding: 2px 6px; border-radius: 6px; font-size: 12px; }
    .ok { background:#c6f6d5; } .out{ background:#fed7d7; } .disc{background:#e2e8f0;}
  </style>
</head>
<body>
  <h1>Block Party — Central (Embedded)</h1>
  <div id="status">Connecting…</div>

  <div style="margin:12px 0;">
    <button onclick="sendAdmin('start')">Start</button>
    <button onclick="sendAdmin('pause')">Pause</button>
    <button onclick="sendAdmin('resume')">Resume</button>
    <button onclick="sendAdmin('reset')">Reset</button>
  </div>

  <h3 id="phaseRound"></h3>
  <table>
    <thead><tr><th>Player</th><th>Block</th><th>In Game</th><th>Score</th><th>Conn</th></tr></thead>
    <tbody id="tbody"></tbody>
  </table>

  <script>
    const ws = new WebSocket(`ws://${location.host}/ws`);
    const state = { phase:'LOBBY', round:-1, players:[] };

    ws.onopen = () => document.getElementById('status').textContent = 'Connected';
    ws.onclose = () => document.getElementById('status').textContent = 'Disconnected';
    ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.type === 'state') {
          state.phase = msg.phase;
          state.round = msg.round;
          state.players = msg.players || [];
          render();
        }
      } catch (_) {}
    };

    function sendAdmin(action) {
      ws.send(JSON.stringify({type:'admin', action}));
    }

    function render() {
      document.getElementById('phaseRound').textContent =
        `Phase: ${state.phase} • Round: ${state.round >= 0 ? state.round : '-'}`;

      const tb = document.getElementById('tbody');
      tb.innerHTML = '';
      for (const p of state.players) {
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td>${p.name}</td>
          <td>${p.blockId}</td>
          <td><span class="badge ${p.inGame?'ok':'out'}">${p.inGame?'IN':'OUT'}</span></td>
          <td>${p.score}</td>
          <td><span class="badge ${p.connected?'ok':'disc'}">${p.connected?'ON':'OFF'}</span></td>`;
        tb.appendChild(tr);
      }
    }
  </script>
</body>
</html>
)HTML";

// -------- Game --------
enum class Phase { LOBBY, RUNNING, PAUSED, DONE };

enum class COMMAND { SHAKE, MINE, PLACE };

struct Player {
  String blockId;
  String name;
  bool   connected = false;
  bool   inGame = false;
  int    score = 0;
  uint32_t lastSeenMs = 0;

  // Round temp:
  bool   reported = false;
  bool   success  = false;
};

struct GameState {
  Phase phase = Phase::LOBBY;
  int   round = -1;
  String currentCmd = "";
  int currentMsWindow = 2500;
  int round0Ms = 2500; // initial time window, player has this much time to respond
  int decayMs  = 150;  // game speeds up by this much each round
  int minMs    = 800;
  uint64_t roundStartMs = 0; // server absolute
  uint64_t deadlineMs   = 0; // server absolute
} gs;

// -------- Storage --------
/// @todo consider using a map instead of vector if we can in ESP32
std::vector<Player> players = {
  {"B1","Sam",  true,  true,  3},
  {"B2","Alex", true,  false, 2},
  {"B3","Riley",true,  true,  5},
};

struct ClientMeta {
  uint32_t id;
  String role;    // "block" | "web"
  String blockId; // set if role == "block"
};
std::vector<ClientMeta> clients;

Player* getPlayer(const String& blockId) {
  for (auto &p : players) if (p.blockId == blockId) return &p;
  return nullptr;
}
Player& addPlayer(const String& blockId) {
  Player* p = getPlayer(blockId);
  if (p) return *p;
  Player np; np.blockId = blockId; np.name = blockId;
  players.push_back(np);
  return players.back();
}

ClientMeta* getClient(uint32_t id) {
  for (auto &c : clients) if (c.id == id) return &c;
  return nullptr;
}
void removeClient(uint32_t id) {
  clients.erase(std::remove_if(clients.begin(), clients.end(),
              [&](const ClientMeta& c){return c.id==id;}), clients.end());
}

// -------- Helpers --------
String phaseToStr(Phase ph) {
  switch (ph) {
    case Phase::LOBBY: return "LOBBY";
    case Phase::RUNNING: return "RUNNING";
    case Phase::PAUSED: return "PAUSED";
    case Phase::DONE: return "DONE";
		default: return "LOBBY";
  }
}

String commandToStr(COMMAND cmd) {
	switch (cmd) {
		case COMMAND::SHAKE: return "SHAKE";
		case COMMAND::MINE: return "MINE";
		case COMMAND::PLACE: return "PLACE";
		default: return "SHAKE";
	}
}

int aliveCount() {
  int n=0;
  for (auto &p: players) if (p.inGame) ++n;
  return n;
}

void broadcastStateToWeb() {
  StaticJsonDocument<1024> doc;
  doc["type"] = "state";
  doc["phase"] = phaseToStr(gs.phase);
  doc["round"] = gs.round;

  JsonArray arr = doc.createNestedArray("players");
  for (auto &p : players) {
    JsonObject o = arr.createNestedObject();
    o["blockId"] = p.blockId;
    o["name"]    = p.name;
    o["inGame"]  = p.inGame;
    o["score"]   = p.score;
    o["connected"] = p.connected;
  }
  String out;
  serializeJson(doc, out);
  
  // Send to web clients
  for (const auto& c : clients) {
    if (c.role == "web") {
      ws.text(c.id, out);
    }
  }
}

/// @brief Send new round info only to blocks that are in the game
void broadcastRoundToBlocks() {
  StaticJsonDocument<256> doc;
  doc["type"] = "round";
  doc["round"] = gs.round;
  doc["cmd"] = gs.currentCmd;
  doc["roundStartMs"] = gs.roundStartMs;    // When round officially starts
  doc["gameTimeMs"] = gs.currentMsWindow;   // How long players have to respond
  doc["deadlineMs"] = gs.deadlineMs;        // When server will end round
  
	String out;
	serializeJson(doc, out);
	for (const auto& c : clients) {
		if (c.role == "block" && c.blockId.length()) {
			Player *p = getPlayer(c.blockId);
			if (p && p->inGame) {
				ws.text(c.id, out);
			}
		}
	}
}

void markRoundStartAndDeadline() {
  // Give 500ms buffer for message delivery and processing
  gs.roundStartMs = millis() + 500;
  gs.deadlineMs   = gs.roundStartMs + gs.currentMsWindow;
}

String randomCmd() {
  uint32_t r = (uint32_t)esp_random() % 3;
  return String(commandToStr((COMMAND)r));
}

// -------- Round lifecycle --------
void resetRoundFlags() {
  for (auto &p : players) { p.reported = false; p.success = false; }
}

void nextRound() {
  if (aliveCount() <= 1) {
    gs.phase = Phase::DONE;
    broadcastStateToWeb();
    return;
  }

  gs.round += 1;
  gs.currentCmd = randomCmd();
  resetRoundFlags();
  markRoundStartAndDeadline();
  broadcastRoundToBlocks();
  broadcastStateToWeb();
}

void endRound() {
  // Eliminate those who did not succeed
  for (auto &p : players) {
    if (!p.inGame) continue;
    if (!p.success) p.inGame = false;
  }
  // Shrink window
  gs.currentMsWindow = max(gs.minMs, gs.currentMsWindow - gs.decayMs);
  broadcastStateToWeb();
}

// -------- WebSocket events --------
void handleBlockHello(AsyncWebSocketClient* client, JsonDocument& doc) {
  String blockId = doc["blockId"] | "";
  auto *meta = getClient(client->id());
  if (!meta) return;
  meta->role = "block";
  meta->blockId = blockId;

  Player &p = addPlayer(blockId);
  p.connected = true;
  p.lastSeenMs = millis();
  p.name = p.name.length() ? p.name : blockId; // default

  broadcastStateToWeb();
}

void handleBlockStatus(JsonDocument& doc) {
  String blockId = doc["blockId"] | "";
  Player *p = getPlayer(blockId);
  if (!p) return;
  p->connected = true;
  p->lastSeenMs = millis();
}

void handleBlockResult(JsonDocument& doc) {
  int round = doc["round"] | -999;
  if (round != gs.round) return;
  String blockId = doc["blockId"] | "";
  Player *p = getPlayer(blockId);
  if (!p || !p->inGame) return;

  bool actionDone = doc["actionDone"] | false;
  p->reported = true;
  p->success  = actionDone;
  if (actionDone) {
    p->score += 1;
  }
  broadcastStateToWeb();
}

void handleAdmin(AsyncWebSocketClient* client, JsonDocument& doc) {
  String action = doc["action"] | "";
  if (action == "start") {
    gs.phase = Phase::RUNNING;
    gs.round = -1;
    gs.round0Ms = doc["round0Ms"] | 2500;
    gs.decayMs  = doc["decayMs"]  | 150;
    gs.minMs    = doc["minMs"]    | 800;
    gs.currentMsWindow = gs.round0Ms;

    // mark all connected as inGame
    for (auto &p : players) { p.inGame = p.connected; p.score = 0; }

    nextRound();
  } else if (action == "pause") {
    if (gs.phase == Phase::RUNNING) gs.phase = Phase::PAUSED;
    broadcastStateToWeb();
  } else if (action == "resume") {
    if (gs.phase == Phase::PAUSED) {
      gs.phase = Phase::RUNNING;
      // optional: extend deadline slightly @todo
      broadcastRoundToBlocks();
      broadcastStateToWeb();
    }
  } else if (action == "reset") {
    gs.phase = Phase::LOBBY;
    gs.round = -1;
    gs.currentMsWindow = gs.round0Ms;
    for (auto &p : players) { p.inGame = false; p.score = 0; p.reported=false; p.success=false; }
    broadcastStateToWeb();
  } else if (action == "rename") {
    String blockId = doc["blockId"] | "";
    String nm  = doc["name"] | "";
    Player *p = getPlayer(blockId);
    if (p && nm.length()) { p->name = nm; broadcastStateToWeb(); }
  }
}

void onWsEvent(AsyncWebSocket       * server,
               AsyncWebSocketClient * client,
               AwsEventType           type,
               void *                 arg,
               uint8_t *              data,
               size_t                 len) {
  if (type == WS_EVT_CONNECT) {
    clients.push_back({client->id(), "", ""});
  } else if (type == WS_EVT_DISCONNECT) {
    auto *meta = getClient(client->id());
    if (meta && meta->role == "block" && meta->blockId.length()) {
      Player *p = getPlayer(meta->blockId);
      if (p) p->connected = false;
      broadcastStateToWeb();
    }
    removeClient(client->id());
  } else if (type == WS_EVT_DATA) {
    // Parse JSON
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) return;

    const char* t = doc["type"] | "";
    if (!strcmp(t, "hello")) {
      handleBlockHello(client, doc);
    } else if (!strcmp(t, "status")) {
      handleBlockStatus(doc);
    } else if (!strcmp(t, "result")) {
      handleBlockResult(doc);
    } else if (!strcmp(t, "admin")) {
      // mark role as web
      auto *meta = getClient(client->id());
      if (meta) meta->role = "web";
      	handleAdmin(client, doc);
    } else {
      broadcastStateToWeb();
    }
  }
}

// -------- Setup HTTP + File System --------
void setupHttp() {
  // Initialize LittleFS
  // if (!LittleFS.begin(true)) {
  //   Serial.println("LittleFS mount failed!");
  //   return;
  // }
  
  // // Check if index.html exists in LittleFS
  // if (!LittleFS.exists("/index.html")) {
  //   Serial.println("Warning: /index.html not found in LittleFS!");
  //   Serial.println("You need to upload the webapp/index.html file to the ESP32's filesystem.");
  //   Serial.println("Create a 'data' folder in your sketch directory, copy index.html there,");
  //   Serial.println("and use 'ESP32 Sketch Data Upload' tool.");
  // }
  
  // // Serve static files from LittleFS
  // server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  
  // // Handle 404 errors - try to serve index.html as fallback
  // server.onNotFound([](AsyncWebServerRequest *req){ 
  //   if (LittleFS.exists("/index.html")) {
  //     req->send(LittleFS, "/index.html", "text/html");
  //   } else {
  //     req->send(404, "text/plain", "File not found. Please upload webapp files to ESP32 filesystem.");
  //   }
  // });

  // Serve embedded page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send_P(200, "text/html", INDEX_HTML);
  });
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  
  Serial.println("HTTP server started");
  Serial.println("Web interface should be available at: http://192.168.4.1:8080");
}

// -------- Setup / Loop --------
uint32_t lastSyncMs = 0;
uint32_t lastPruneMs = 0;
uint32_t lastRoundCheckMs = 0;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 8);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  setupHttp();
}

void loop() {
  // 1) push time sync to blocks (~1s)
  if (millis() - lastSyncMs > 1000) {
    lastSyncMs = millis();
    StaticJsonDocument<128> doc;
    doc["type"] = "sync";
    doc["serverTimeMs"] = (uint64_t)millis();
    String out; serializeJson(doc, out);
    ws.textAll(out);
  }

  // 2) prune disconnected players if stale
  if (millis() - lastPruneMs > 2000) {
    lastPruneMs = millis();
    for (auto &p : players) {
      if (p.connected && (millis() - p.lastSeenMs > 5000)) {
        p.connected = false;
      }
    }
  }

  // 3) round timing gates (end & next)
  if (gs.phase == Phase::RUNNING && millis() - lastRoundCheckMs > 50) {
    lastRoundCheckMs = millis();
    if (millis() > gs.deadlineMs + 20) {
      endRound();
      // Schedule next round start time (non-blocking approach)
      gs.roundStartMs = millis() + 800;  // 800ms delay before next round
      gs.phase = Phase::PAUSED;  // Temporarily pause to wait for next round
    }
  }

  // 4) Handle scheduled next round start
  if (gs.phase == Phase::PAUSED && millis() >= gs.roundStartMs) {
    gs.phase = Phase::RUNNING;
    nextRound();
  }

  ws.cleanupClients();
}
