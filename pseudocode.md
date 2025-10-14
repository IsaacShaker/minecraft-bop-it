# Minecraft Bop-It Pseudocode

## Block.ino (ESP32 Game Block)

### Setup
```
INITIALIZE hardware pins (LEDs, button, RFID, speaker)
LOAD block ID from persistent storage OR generate new unique ID
CONNECT to WiFi network
ESTABLISH WebSocket connection to central server
SET state to REGISTERED when connected
```

### Main Loop State Machine
```
WHILE running:
    HANDLE WebSocket messages
    SEND periodic status updates to server
    
    SWITCH current_state:
        CASE WAIT_ROUND:
            IF server_time >= round_start_time:
                START round execution
        
        CASE EXECUTING:
            IF timeout_expired:
                SEND failure result to server
                SET state to REPORTED
            ELSE IF correct_action_detected:
                STOP timer
                SEND success result to server  
                SET state to REPORTED
        
        CASE REPORTED:
            WAIT for next round message
```

### Action Detection
```
FUNCTION detect_mine():
    READ button with debounce logic
    RETURN true if button pressed

FUNCTION detect_shake():
    READ accelerometer data
    RETURN true if shake detected

FUNCTION detect_place():
    READ RFID sensor
    RETURN true if block placed correctly
```

### Message Handling
```
ON WebSocket message received:
    IF message_type == "round":
        PARSE round data (command, timing, round number)
        START countdown timer
        PLAY voice command
        SET state to EXECUTING
    
    IF message_type == "sync":
        UPDATE server time offset for synchronization
```

---

## Central.ino (ESP32 Server/Controller)

### Setup
```
CREATE WiFi access point "BlockParty"
START HTTP server on port 8080
START WebSocket server
INITIALIZE game state (LOBBY phase)
SERVE web interface files
```

### Main Loop
```
WHILE running:
    BROADCAST time sync to all blocks every 1 second
    CLEANUP disconnected players every 2 seconds
    
    IF game_phase == RUNNING:
        CHECK if round deadline passed:
            ELIMINATE failed players
            REDUCE time window for next round
            SCHEDULE next round OR end game
```

### Game Flow Management
```
FUNCTION start_game():
    SET all connected blocks as active players
    RESET scores to zero
    BEGIN first round

FUNCTION next_round():
    IF only_one_player_remaining:
        END game
    ELSE:
        INCREMENT round number
        CHOOSE random command (SHAKE/MINE/PLACE)
        CALCULATE round timing
        BROADCAST round info to active blocks
        UPDATE web interface

FUNCTION end_round():
    ELIMINATE players who failed or timed out
    REDUCE time window for difficulty increase
    UPDATE player scores and web display
```

### WebSocket Message Handling
```
ON client connects:
    ADD to client list
    
ON block sends "hello":
    REGISTER block with unique ID
    ADD to player roster
    UPDATE web interface

ON block sends "result":
    RECORD player performance (success/fail)
    UPDATE score if successful
    UPDATE web interface

ON web admin sends command:
    EXECUTE game control (start/pause/reset)
    UPDATE game state accordingly
```

### Player Management
```
MAINTAIN list of players with:
    - Block ID and display name
    - Connection status
    - Current game participation
    - Score and round results

BROADCAST game state to web interface:
    - Current phase and round
    - Player list with scores
    - Connection statuses
```

## Key Communication Flow

1. **Block Registration**: Block connects → sends hello → server adds to player list
2. **Game Start**: Web admin starts game → server marks active players → begins rounds
3. **Round Execution**: Server broadcasts round → blocks execute → blocks report results
4. **Elimination**: Server processes results → eliminates failed players → starts next round
5. **Game End**: When ≤1 player remains → display final results

## Timing Synchronization

- Server broadcasts current time every second
- Blocks calculate time offset to synchronize clocks
- Round timing uses server time to ensure fair play across all blocks
- Built-in delays account for network transmission time