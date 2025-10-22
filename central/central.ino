// ================= Central.ino (ESP32) =================
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <vector>

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

// ---- Big embedded page (edit freely) ----
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Block Party</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body { font-family: system-ui, Arial, sans-serif; margin: 16px; }
    #status { padding: 6px 10px; display:inline-block; border-radius:6px; background:#eef; }
    .badge { padding: 2px 6px; border-radius: 6px; font-size: 12px; }
    .ok { background:#c6f6d5; }
    .out { background:#fed7d7; }
    .disc { background:#e2e8f0; }
    table { border-collapse: collapse; width: 100%; margin-top:12px; }
    th, td { border-bottom: 1px solid #eee; text-align: left; padding: 6px 8px; }
    .column-container { display:flex; flex-direction: column; }
    .row-container { display:flex; flex-direction: row; gap:8px; align-items: center; flex-wrap: wrap; }
    input[type=number]{ width:90px; }
    button { 
      padding: 4px 8px; 
      border-radius: 6px; 
      font-size: 12px; 
      border: none; 
      background: #e2e8f0; 
      cursor: pointer; 
      font-family: inherit;
    }
    button:hover { background: #cbd5e0; }
    button:active { background: #a0aec0; }
  </style>
</head>
<body>
  <h1>Block Party — Multiplayer Bop-It</h1>
  <div id="status">Connecting…</div>

  <div class="column-container" style="margin:12px 0;">
    <div class="row-container">
      <h3>Admin Controls:</h3>
      <button onclick="startGame()">Start</button>
      <button onclick="pauseGame()">Pause</button>
      <button onclick="resumeGame()">Resume</button>
      <button onclick="resetGame()">Reset</button>
    </div>
    <div class="row-container">
      <h3>Game Settings:</h3>
      <label>Round0(ms) <input id="round0" type="number" value="2500"></label>
      <label>Decay(ms) <input id="decay" type="number" value="150"></label>
      <label>Min(ms)   <input id="minms" type="number" value="800"></label>
    </div>
  </div>

  <h3 id="phaseRound"></h3>
  <h3 id="Round"></h3>

  <table id="table">
    <thead><tr><th>Player</th><th>Block</th><th>In Game</th><th>Score</th><th>Conn</th><th>Rename</th></tr></thead>
    <tbody></tbody>
  </table>

  <script>
    const ws = new WebSocket(`ws://${window.location.host}/ws`);
    const state = { phase:'LOBBY', round:0, players:[] };

    ws.onopen = () => {
      document.getElementById('status').textContent = 'Connected';
      // Identify this client as a web interface
      ws.send(JSON.stringify({type: 'web-hello', clientType: 'web'}));
    };
    ws.onclose = () => {
      document.getElementById('status').textContent = 'Disconnected';
    };
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'state') {
          state.phase = msg.phase;
          state.round = msg.round;
          state.players = msg.players || [];
          render();
        }
      } catch(e) {}
    };

    function sendAdmin(payload) {
      ws.send(JSON.stringify(Object.assign({type:'admin'}, payload)));
    }

    function startGame() {
      const round0 = +document.getElementById('round0').value || 2500;
      const decay  = +document.getElementById('decay').value || 150;
      const minms  = +document.getElementById('minms').value || 800;
      sendAdmin({action:'start', round0Ms:round0, decayMs:decay, minMs:minms});
    }

    function pauseGame(){ sendAdmin({action:'pause'}); }
    function resumeGame(){ sendAdmin({action:'resume'}); }
    function resetGame(){ sendAdmin({action:'reset'}); }

    function renameBlock(bid, name){
      sendAdmin({action:'rename', blockId:bid, name});
    }

    function render() {
      document.getElementById('phaseRound').textContent = `Phase: ${state.phase}`;
      document.getElementById('Round').textContent = `Round: ${state.round >= 0 ? state.round : '-'}`;

      const tb = document.querySelector('#table tbody');
      tb.innerHTML = '';
      const sorted = [...state.players].sort((a,b)=> b.score - a.score);
      for (const p of sorted) {
        const tr = document.createElement('tr');
        const inBadge = `<span class="badge ${p.inGame?'ok':'out'}">${p.inGame?'IN':'OUT'}</span>`;
        const cBadge  = `<span class="badge ${p.connected?'ok':'disc'}">${p.connected?'ON':'OFF'}</span>`;

        tr.innerHTML = `
          <td>${p.name}</td>
          <td>${p.blockId}</td>
          <td>${inBadge}</td>
          <td>${p.score}</td>
          <td>${cBadge}</td>
          <td>
            <input size="10" value="${p.name}" id="nm-${p.blockId}">
            <button onclick="renameBlock('${p.blockId}', document.getElementById('nm-${p.blockId}').value)">Save</button>
          </td>`;
        tb.appendChild(tr);
      }
    }
  </script>
</body>
</html>
)HTML";

// -------- Game --------
enum class Phase { LOBBY, RUNNING, WAITING_NEXT_ROUND, PAUSED, DONE };

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
  bool pauseQueued = false; // flag to pause after current round ends
} gs;

// -------- Storage --------
/// @todo consider using a map instead of vector if we can in ESP32
std::vector<Player> players;

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
    case Phase::WAITING_NEXT_ROUND: return "WAITING_NEXT_ROUND";
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

String buildGameStateMessage() {
  JSONVar doc;
  doc["type"] = "state";
  doc["phase"] = phaseToStr(gs.phase);
  doc["round"] = gs.round;

  JSONVar arr;
  int i = 0;
  for (auto &p : players) {
    JSONVar playerObj;
    playerObj["blockId"] = p.blockId;
    playerObj["name"]    = p.name;
    playerObj["inGame"]  = p.inGame;
    playerObj["score"]   = p.score;
    playerObj["connected"] = p.connected;
    arr[i++] = playerObj;
  }
  doc["players"] = arr;
  return JSON.stringify(doc);
}

void broadcastStateToWeb() {
  String message = buildGameStateMessage();
  // Send to all web clients
  for (const auto& c : clients) {
    if (c.role == "web") {
      ws.text(c.id, message);
    }
  }
}

void broadcastStateToWeb(uint32_t clientId) {
  // Check if clientId exists in clients vector
  auto it = std::find_if(clients.begin(), clients.end(),
                         [clientId](const ClientMeta& c){ return c.id == clientId; });
  if (it == clients.end()) return; // client not found

  String message = buildGameStateMessage();
  ws.text(clientId, message);
}

/// @brief Send new round info only to blocks that are in the game
void broadcastRoundToBlocks() {
  JSONVar doc;
  doc["type"] = "round";
  doc["round"] = gs.round;
  doc["cmd"] = gs.currentCmd;
  doc["roundStartMs"] = (unsigned long)gs.roundStartMs;    // When round officially starts
  doc["gameTimeMs"] = gs.currentMsWindow;   // How long players have to respond
  doc["deadlineMs"] = (unsigned long)gs.deadlineMs;        // When server will end round
  
	String out = JSON.stringify(doc);
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
void handleBlockHello(AsyncWebSocketClient* client, JSONVar& doc) {
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
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

void handleWebHello(AsyncWebSocketClient* client, JSONVar& doc) {
  auto *meta = getClient(client->id());
  if (!meta) return;
  meta->role = "web";
  meta->blockId = ""; // web clients don't have block IDs
  
  Serial.printf("Web client connected: %u\n", client->id());
  // Send current state to the newly connected web client
  broadcastStateToWeb(client->id());
}

void handleBlockStatus(JSONVar& doc) {
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  Player *p = getPlayer(blockId);
  if (!p) return;
  p->connected = true;
  p->lastSeenMs = millis();
}

void handleBlockResult(JSONVar& doc) {
  if (gs.phase != Phase::RUNNING && gs.phase != Phase::WAITING_NEXT_ROUND) return;

  int round = doc.hasOwnProperty("round") ? (int)doc["round"] : -999;
  if (round != gs.round) return;
  String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
  Player *p = getPlayer(blockId);
  if (!p || !p->inGame) return;

  bool actionDone = doc.hasOwnProperty("actionDone") ? (bool)doc["actionDone"] : false;
  p->reported = true;
  p->success = actionDone;
  if (actionDone) {
    p->score += 1;
  }
  broadcastStateToWeb();
}

void handleAdmin(AsyncWebSocketClient* client, JSONVar& doc) {
  // Verify that the client is authenticated as a web client
  auto *meta = getClient(client->id());
  if (!meta || meta->role != "web") {
    Serial.printf("Unauthorized admin attempt from client %u (role: %s)\n", 
                  client->id(), meta ? meta->role.c_str() : "unknown");
    return;
  }
  
  String action = doc.hasOwnProperty("action") ? (const char*)doc["action"] : "";
  if (action == "start") {
    gs.phase = Phase::RUNNING;
    gs.round = -1;
    gs.round0Ms = doc.hasOwnProperty("round0Ms") ? (int)doc["round0Ms"] : 2500;
    gs.decayMs  = doc.hasOwnProperty("decayMs") ? (int)doc["decayMs"] : 150;
    gs.minMs    = doc.hasOwnProperty("minMs") ? (int)doc["minMs"] : 800;
    gs.currentMsWindow = gs.round0Ms;

    // mark all connected as inGame
    for (auto &p : players) { p.inGame = p.connected; p.score = 0; }

    nextRound();
  } else if (action == "pause") {
    // Queue the pausing until this round is over
    gs.pauseQueued = true;
    broadcastStateToWeb();
  } else if (action == "resume") {
    gs.pauseQueued = false; // Clear any queued pause
    if (gs.phase == Phase::PAUSED) {
      gs.phase = Phase::WAITING_NEXT_ROUND;
      broadcastStateToWeb();
    }
  } else if (action == "reset") {
    gs.phase = Phase::LOBBY;
    gs.round = -1;
    gs.currentMsWindow = gs.round0Ms;
    gs.pauseQueued = false;
    for (auto &p : players) { p.inGame = false; p.score = 0; p.reported=false; p.success=false; }
    broadcastStateToWeb();
  } else if (action == "rename") {
    if (gs.phase != Phase::LOBBY) return; // only allow rename in lobby
    String blockId = doc.hasOwnProperty("blockId") ? (const char*)doc["blockId"] : "";
    String nm  = doc.hasOwnProperty("name") ? (const char*)doc["name"] : "";
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
    // Send current state only to the newly connected client
    broadcastStateToWeb(client->id());
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
      broadcastStateToWeb();
    }
  }
}

// -------- Setup HTTP + File System --------
void setupHttp() {
  // Serve embedded page
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
      gs.phase = Phase::WAITING_NEXT_ROUND;  // Temporarily pause to wait for next round
      broadcastStateToWeb();
    }
  }

  // 4) Handle scheduled next round start
  if (gs.phase == Phase::WAITING_NEXT_ROUND && millis() >= gs.roundStartMs) {
    if (gs.pauseQueued) {
        gs.pauseQueued = false;
        gs.phase = Phase::PAUSED;
        broadcastStateToWeb();
    }
    else {
      gs.phase = Phase::RUNNING;
      nextRound();
    }
  }

  ws.cleanupClients();
}
