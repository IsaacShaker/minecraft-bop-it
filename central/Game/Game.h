#ifndef GAME_H
#define GAME_H

#include <Arduino.h>
#include <Arduino_JSON.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include <memory>
#include "../Player/Player.h"

enum class Phase { LOBBY, RUNNING, WAITING_NEXT_ROUND, PAUSED, DONE };
enum class COMMAND { SHAKE, MINE, PLACE };

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
  String m_current_cmd;
  int m_current_ms_window;
  int m_round0_ms;
  int m_decay_ms;
  int m_min_ms;
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

  const String& getCurrentCmd() const { return m_current_cmd; }
  void setCurrentCmd(const String& cmd);
  
  // Timing
  int getCurrentMsWindow() const { return m_current_ms_window; }
  void setCurrentMsWindow(int window);

  int getRound0Ms() const { return m_round0_ms; }
  void setRound0Ms(int ms);

  int getDecayMs() const { return m_decay_ms; }
  void setDecayMs(int ms);

  int getMinMs() const { return m_min_ms; }
  void setMinMs(int ms);

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
  String randomCmd();
  
  // Admin actions
  void startGame(int round0Ms = 2500, int decayMs = 150, int minMs = 800);
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
  static String commandToStr(COMMAND cmd);
  
};

#endif // GAME_H
