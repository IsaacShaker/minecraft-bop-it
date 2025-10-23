#include "Player.h"
#include "../Game/Game.h"

Player::Player(const String& blockId, Game* game) 
  : m_block_id(blockId), m_name(blockId), m_connected(false), m_in_game(false), 
    m_score(0), m_last_seen_ms(0), m_reported(false), m_success(false), m_game(game) {
}

void Player::setName(const String& name) {
  if (m_name != name) {
    m_name = name;
    notifyChange();
  }
}

void Player::setConnected(bool connected) {
  if (m_connected != connected) {
    m_connected = connected;
    notifyChange();
  }
}

void Player::setInGame(bool inGame) {
  if (m_in_game != inGame) {
    m_in_game = inGame;
    notifyChange();
  }
}

void Player::setScore(int score) {
  if (m_score != score) {
    m_score = score;
    notifyChange();
  }
}

void Player::incrementScore() {
  m_score++;
  notifyChange();
}

void Player::setLastSeenMs(uint32_t lastSeenMs) {
  m_last_seen_ms = lastSeenMs;
}

void Player::setReported(bool reported) {
  if (m_reported != reported) {
    m_reported = reported;
  }
}

void Player::setSuccess(bool success) {
  if (m_success != success) {
    m_success = success;
  }
}

void Player::resetRoundFlags() {
  bool changed = (m_reported != false) || (m_success != false);
  m_reported = false;
  m_success = false;
}

void Player::notifyChange() {
  if (m_game) {
    m_game->broadcastStateToWeb();
  }
}