# Central Server (ESP32)

This directory contains the central server code for the Block Party multiplayer Bop-It game.

## File Structure

### Core Files
- `central.ino` - Main Arduino sketch for ESP32

### Game Logic
- `Game/Game.h` / `Game/Game.cpp` - Game state management and logic
- `Player/Player.h` / `Player/Player.cpp` - Player state management

### Web Interface
- `Web/index.html` - Source HTML file for the web interface
- `Web/generate_web_header.sh` - Script to generate web_interface.h from index.html
- `Web/web_interface.h` - Auto-generated header containing the web interface HTML

### Game Class
- Manages overall game state (phase, round, timing)
- Handles player collection and client connections
- Provides automatic state broadcasting on changes
- Encapsulates all game logic (start, pause, reset, etc.)

### Player Class  
- Manages individual player state (name, score, connection status)
- Automatic broadcasting when state changes through setters
- Prevents direct access to internal state

## Building with Arduino IDE

### Preparation
Before opening the sketch in Arduino IDE, generate the web interface header:

```bash
cd Web
./generate_web_header.sh
cd ..
```

### Arduino IDE Compilation
1. Open `central.ino` in Arduino IDE
2. Make sure you have the required libraries installed:
   - ESPAsyncWebServer
   - Arduino_JSON
3. Select your ESP32 board and compile normally

### Note on Subdirectories
The project is organized into subdirectories for better code organization:
- `Game/` - Game logic classes
- `Player/` - Player management classes  
- `Web/` - Web interface files

## Development Workflow

1. Edit game logic in `Game/Game.cpp` or player logic in `Player/Player.cpp`
2. Edit the web interface in `Web/index.html`
3. Run `./Web/generate_web_header.sh` to generate the web header
    - This will create `Web/web_interface.h` from `Web/index.html`.
4. Open and compile `central.ino` in Arduino IDE

## Web Interface

The web interface is accessible at `http://192.168.4.1` when the ESP32 is running in AP mode.

Default WiFi credentials:
- SSID: `BlockParty`
- Password: `craft123`
