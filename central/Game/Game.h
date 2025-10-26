#ifndef GAME_H
#define GAME_H

#include <Arduino.h>
#include <Arduino_JSON.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include <memory>
#include "../Player/Player.h"

enum class Phase { LOBBY, RUNNING, WAITING_NEXT_ROUND, PAUSED, DONE };
enum class Command { SHAKE, MINE, PLACE };

struct ClientMeta {
  uint32_t id;
  String role;    // "block" | "web"
  String blockId; // set if role == "block"
};

class Game {
private:
  // Game state
  Phase m_phase;
  int m_round;
  Command m_current_cmd;
  uint32_t m_current_ms_window;
  uint32_t m_round0_ms;
  uint32_t m_decay_ms;
  uint32_t m_min_ms;
  uint64_t m_round_start_ms;
  uint64_t m_deadline_ms;
  bool m_pause_queued;
  
  // Players and clients
  std::vector<std::unique_ptr<Player>> m_players;
  std::vector<ClientMeta> m_clients;
  
  // WebSocket reference
  AsyncWebSocket* m_ws;

public:
  // Constructor
  Game(AsyncWebSocket* ws);
  
  // Phase management
  Phase getPhase() const { return m_phase; }
  void setPhase(Phase phase);
  
  // Round management
  int getRound() const { return m_round; }
  void setRound(int round);

  Command getCurrentCmd() const { return m_current_cmd; }
  void setCurrentCmd(Command cmd);
  
  // Timing
  uint32_t getCurrentMsWindow() const { return m_current_ms_window; }
  void setCurrentMsWindow(uint32_t window);

  uint32_t getRound0Ms() const { return m_round0_ms; }
  void setRound0Ms(uint32_t ms);

  uint32_t getDecayMs() const { return m_decay_ms; }
  void setDecayMs(uint32_t ms);

  uint32_t getMinMs() const { return m_min_ms; }
  void setMinMs(uint32_t ms);

  uint64_t getRoundStartMs() const { return m_round_start_ms; }
  void setRoundStartMs(uint64_t ms);

  uint64_t getDeadlineMs() const { return m_deadline_ms; }
  void setDeadlineMs(uint64_t ms);

  bool isPauseQueued() const { return m_pause_queued; }
  void setPauseQueued(bool queued);
  
  // Player management
  Player* getPlayer(const String& blockId);
  Player& addPlayer(const String& blockId);
  const std::vector<std::unique_ptr<Player>>& getPlayers() const { return m_players; }
  
  // Client management
  ClientMeta* getClient(uint32_t id);
  void addClient(uint32_t id);
  void removeClient(uint32_t id);
  const std::vector<ClientMeta>& getClients() const { return m_clients; }
  
  // Game logic
  int aliveCount() const;
  void resetRoundFlags();
  void nextRound();
  void endRound();
  void markRoundStartAndDeadline();
  Command randomCmd();
  
  // Admin actions
  void startGame(uint32_t round0Ms = 2500, uint32_t decayMs = 150, uint32_t minMs = 800);
  void pauseGame();
  void resumeGame();
  void resetGame();
  void renamePlayer(const String& blockId, const String& name);
  
  // Broadcasting
  void broadcastStateToWeb();
  void broadcastStateToWeb(uint32_t clientId);
  void broadcastRoundToBlocks();
  
  // Helper functions
  String buildGameStateMessage();
  static String phaseToStr(Phase ph);
  static String commandToStr(Command cmd);
  
};

#endif // GAME_H
