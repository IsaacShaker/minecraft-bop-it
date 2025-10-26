#include "Game.h"

Game::Game(AsyncWebSocket* ws) 
  : m_phase(Phase::LOBBY), m_round(0), m_current_cmd(Command::SHAKE), m_current_ms_window(2500),
    m_round0_ms(2500), m_decay_ms(150), m_min_ms(800), m_round_start_ms(0), 
    m_deadline_ms(0), m_pause_queued(false), m_ws(ws) {
}

// Phase management
void Game::setPhase(Phase phase) {
  if (m_phase != phase) {
    m_phase = phase;
    broadcastStateToWeb();
  }
}

// Round management
void Game::setRound(int round) {
  if (m_round != round) {
    m_round = round;
    broadcastStateToWeb();
  }
}

void Game::setCurrentCmd(Command cmd) {
  if (m_current_cmd != cmd) {
    m_current_cmd = cmd;
    broadcastStateToWeb();
  }
}

// Timing setters
void Game::setCurrentMsWindow(uint32_t window) {
  if (m_current_ms_window != window) {
    m_current_ms_window = window;
  }
}

void Game::setRound0Ms(uint32_t ms) {
  if (m_round0_ms != ms) {
    m_round0_ms = ms;
  }
}

void Game::setDecayMs(uint32_t ms) {
  if (m_decay_ms != ms) {
    m_decay_ms = ms;
  }
}

void Game::setMinMs(uint32_t ms) {
  if (m_min_ms != ms) {
    m_min_ms = ms;
  }
}

void Game::setRoundStartMs(uint64_t ms) {
  m_round_start_ms = ms;
}

void Game::setDeadlineMs(uint64_t ms) {
  m_deadline_ms = ms;
}

void Game::setPauseQueued(bool queued) {
  if (m_pause_queued != queued) {
    m_pause_queued = queued;
  }
}

// Player management
Player* Game::getPlayer(const String& blockId) {
  for (auto& p : m_players) {
    if (p->getBlockId() == blockId) {
      return p.get();
    }
  }
  return nullptr;
}

Player& Game::addPlayer(const String& blockId) {
  Player* existing = getPlayer(blockId);
  if (existing) return *existing;
  
  auto newPlayer = std::make_unique<Player>(blockId, this);
  Player* playerPtr = newPlayer.get();
  m_players.push_back(std::move(newPlayer));
  broadcastStateToWeb();
  return *playerPtr;
}

// Client management
ClientMeta* Game::getClient(uint32_t id) {
  for (auto& c : m_clients) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

void Game::addClient(uint32_t id) {
  m_clients.push_back({id, "", ""});
}

void Game::removeClient(uint32_t id) {
  m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(),
                [id](const ClientMeta& c){ return c.id == id; }), 
                m_clients.end());
}

// Game logic
int Game::aliveCount() const {
  int count = 0;
  for (const auto& p : m_players) {
    if (p->isInGame()) count++;
  }
  return count;
}

void Game::resetRoundFlags() {
  for (auto& p : m_players) {
    p->resetRoundFlags();
  }
}

void Game::nextRound() {
  if (aliveCount() <= 1) {
    setPhase(Phase::DONE);
    return;
  }

  setCurrentCmd(randomCmd());
  resetRoundFlags();
  
  setRound(m_round + 1);
  markRoundStartAndDeadline();
  broadcastRoundToBlocks();
}

void Game::endRound() {
  // Eliminate players who didn't succeed
  for (auto& p : m_players) {
    if (!p->isInGame()) continue;
    if (!p->wasSuccessful()) {
      p->setInGame(false);
    }
  }
  
  // Shrink window - ensure it doesn't go below minimum
  uint32_t newWindow = (m_current_ms_window > m_decay_ms) ? 
                       m_current_ms_window - m_decay_ms : m_min_ms;
  setCurrentMsWindow(max(m_min_ms, newWindow));
}

void Game::markRoundStartAndDeadline() {
  setRoundStartMs(millis() + 500); // prepare and send command 500ms before start (to account for transmission delay)
  setDeadlineMs(m_round_start_ms + m_current_ms_window);
}

Command Game::randomCmd() {
  uint32_t r = (uint32_t)esp_random() % 3;
  return (Command)r;
}

// Admin actions
void Game::startGame(uint32_t round0Ms, uint32_t decayMs, uint32_t minMs) {
  if (m_phase != Phase::LOBBY) return; // Can only start from lobby

  setPhase(Phase::RUNNING);
  setRound(0);
  setRound0Ms(round0Ms);
  setDecayMs(decayMs);
  setMinMs(minMs);
  setCurrentMsWindow(round0Ms);
  setPauseQueued(false);

  // Mark all connected players as in-game and reset scores
  for (auto& p : m_players) {
    p->setInGame(p->isConnected());
    p->setScore(0);
  }

  nextRound();
}

void Game::pauseGame() {
  setPauseQueued(true);
}

void Game::resumeGame() {
  setPauseQueued(false);
  if (m_phase == Phase::PAUSED) {
    setPhase(Phase::WAITING_NEXT_ROUND);
  }
}

void Game::resetGame() {
  setPhase(Phase::LOBBY);
  setRound(0);
  setCurrentMsWindow(m_round0_ms);
  setPauseQueued(false);
  
  for (auto& p : m_players) {
    p->setInGame(false);
    p->setScore(0);
    p->resetRoundFlags();
  }
}

void Game::renamePlayer(const String& blockId, const String& name) {
  if (m_phase != Phase::LOBBY) return; // Only allow rename in lobby
  
  Player* p = getPlayer(blockId);
  if (p && name.length()) {
    p->setName(name);
  }
}

// Broadcasting
void Game::broadcastStateToWeb() {
  if (!m_ws) return;
  
  String message = buildGameStateMessage();
  for (const auto& c : m_clients) {
    if (c.role == "web") {
      m_ws->text(c.id, message);
    }
  }
}

void Game::broadcastStateToWeb(uint32_t clientId) {
  if (!m_ws) return;
  
  auto it = std::find_if(m_clients.begin(), m_clients.end(),
                         [clientId](const ClientMeta& c){ return c.id == clientId; });
  if (it == m_clients.end()) return;

  if (it->role != "web") return;

  String message = buildGameStateMessage();
  m_ws->text(clientId, message);
}

void Game::broadcastRoundToBlocks() {
  if (!m_ws) return;
  
  JSONVar doc;
  doc["type"] = "round";
  doc["round"] = m_round;
  doc["cmd"] = commandToStr(m_current_cmd);
  doc["roundStartMs"] = (unsigned long)m_round_start_ms;
  doc["gameTimeMs"] = m_current_ms_window;
  doc["deadlineMs"] = (unsigned long)m_deadline_ms;
  
  String out = JSON.stringify(doc);
  for (const auto& c : m_clients) {
    if (c.role == "block" && c.blockId.length()) {
      Player* p = getPlayer(c.blockId);
      if (p && p->isInGame()) {
        m_ws->text(c.id, out);
      }
    }
  }
}

String Game::buildGameStateMessage() {
  JSONVar doc;
  doc["type"] = "state";
  doc["phase"] = phaseToStr(m_phase);
  doc["round"] = m_round;
  doc["currentCmd"] = commandToStr(m_current_cmd);

  JSONVar arr;
  int i = 0;
  for (const auto& p : m_players) {
    JSONVar playerObj;
    playerObj["blockId"] = p->getBlockId();
    playerObj["name"] = p->getName();
    playerObj["inGame"] = p->isInGame();
    playerObj["score"] = p->getScore();
    playerObj["connected"] = p->isConnected();
    playerObj["reported"] = p->hasReported();
    playerObj["successful"] = p->wasSuccessful();
    arr[i++] = playerObj;
  }
  doc["players"] = arr;
  return JSON.stringify(doc);
}

String Game::phaseToStr(Phase ph) {
  switch (ph) {
    case Phase::LOBBY: return "LOBBY";
    case Phase::RUNNING: return "RUNNING";
    case Phase::WAITING_NEXT_ROUND: return "WAITING_NEXT_ROUND";
    case Phase::PAUSED: return "PAUSED";
    case Phase::DONE: return "DONE";
    default: return "LOBBY";
  }
}

String Game::commandToStr(Command cmd) {
  switch (cmd) {
    case Command::SHAKE: return "SHAKE";
    case Command::MINE: return "MINE";
    case Command::PLACE: return "PLACE";
    default: return "SHAKE";
  }
}
