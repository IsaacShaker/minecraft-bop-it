#ifndef PLAYER_H
#define PLAYER_H

#include <Arduino.h>

// Forward declaration to avoid circular dependency
class Game;

class Player {
private:
  // Player state
  String m_block_id;
  String m_name;
  bool m_connected;
  bool m_in_game;
  int m_score;
  uint32_t m_last_seen_ms;

  // Temporary round variables
  bool m_reported;
  bool m_success;

  // Reference to game for broadcasting
  Game* m_game;

public:
  // Constructor
  Player(const String& blockId, Game* game = nullptr);
  
  // Getters
  const String& getBlockId() const { return m_block_id; }
  const String& getName() const { return m_name; }
  bool isConnected() const { return m_connected; }
  bool isInGame() const { return m_in_game; }
  int getScore() const { return m_score; }
  uint32_t getLastSeenMs() const { return m_last_seen_ms; }
  bool hasReported() const { return m_reported; }
  bool wasSuccessful() const { return m_success; }
  
  // Setters (with automatic broadcasting)
  void setName(const String& name);
  void setConnected(bool connected);
  void setInGame(bool inGame);
  void setScore(int score);
  void incrementScore();
  void setLastSeenMs(uint32_t lastSeenMs);
  void setReported(bool reported);
  void setSuccess(bool success);
  void setGame(Game* game) { m_game = game; }
  
  // Round management
  void resetRoundFlags();
  
private:
  void notifyChange();
	
};

#endif // PLAYER_H