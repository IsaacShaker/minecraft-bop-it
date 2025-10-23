# Block Party - Multiplayer Bop-It Game

A multiplayer reaction-based game using ESP32 microcontrollers inspired by the classic Bop-It toy and Minecraft themes. Players compete to perform the correct actions (shake, mine, place) within decreasing time limits as the game speeds up each round.

## 🎮 Game Overview

Block Party is a fast-paced elimination game where:
- Multiple players use ESP32-powered "blocks" as controllers
- A central ESP32 server hosts a WiFi network and web interface
- Players must perform the correct action (shake, mine, place) when prompted
- Each round gets faster - last player standing wins!
- Real-time leaderboard and spectator mode via web interface

## 🛠️ Hardware Requirements

- **1x ESP32 board** - Central server (hosts WiFi network and web interface)
- **2+ ESP32 boards** - Player blocks (one per player) 
- **USB cables** for programming the ESP32s
- **Computer** with Arduino IDE for flashing firmware

## ⚙️ Software Setup

### 1. Install Arduino IDE
Download and install the [Arduino IDE](https://www.arduino.cc/en/software) (version 2.0+ recommended)

### 2. Add ESP32 Board Support
1. Open Arduino IDE → **File** → **Preferences**
2. Add this URL to **Additional Board Manager URLs**:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Go to **Tools** → **Board** → **Boards Manager**
4. Search for "ESP32" and install **ESP32 by Espressif Systems**

### 3. Install Required Libraries
Go to **Tools** → **Manage Libraries** and install:
- **ESPAsyncWebServer** by ESP32Async
- **WebSockets** by Markus Sattler
- **Arduino_JSON** by Arduino

### 4. Flash the Firmware

#### Central Server (1 ESP32)
1. Navigate to the `central/` directory
2. Generate the web interface header:
   ```bash
   cd Web
   ./generate_web_header.sh
   cd ..
   ```
3. Open `central.ino` in Arduino IDE
4. Select your ESP32 board and port
5. Upload the sketch

#### Player Blocks (2+ ESP32s)
1. Navigate to the `block/` directory  
2. Open `block.ino` in Arduino IDE
3. Upload to each player ESP32
4. Each block will get a unique ID automatically

## 🚀 Getting Started - Game Setup

### ⚡ **Power Up**
- Plug in the **central server ESP32** (powers the WiFi network)
- Power up all **player block ESP32s** (they'll connect automatically)

### 📱 **Connect to Game Network**
1. **Open WiFi settings** on your phone, tablet, or laptop
2. **Find "BlockParty" network** in the available WiFi list
   - **If not visible**: Look for "Other Networks" or "Add Network" option
   - **Manually enter**: SSID: `BlockParty`, Password: `craft123`
3. **Connect** to the BlockParty network
   - Your device may warn about "no internet" - this is normal!

### 🌐 **Access Game Interface**
4. **Open your web browser** (Chrome, Safari, Firefox, etc.)
5. **Navigate to**: `http://192.168.4.1`

### 🎮 **Start Playing**
6. **Configure game settings** (optional):
   - **Round0(ms)**: Initial reaction time (default: 2500ms)
   - **Decay(ms)**: How much faster each round gets (default: 150ms)
   - **Min(ms)**: Minimum reaction time (default: 800ms)
7. **Wait for players** to connect with their ESP32 blocks
8. **Click "Start"** when ready to begin
9. **Watch the leaderboard** and enjoy the competition! 🏆

### 💡 **Pro Tips**
- Multiple people can open the web interface to spectate
- The game automatically eliminates players who are too slow
- Players are ranked by score on the web interface
- Use "Pause/Resume" for breaks during long games
    - note: pausing happens after current round finishes

## 🎯 How to Play

1. **Listen for the command**: SHAKE, MINE, or PLACE
2. **Perform the action** on your ESP32 block:
   - **SHAKE**: Shake/move your ESP32 block
   - **MINE**: Quick button press
   - **PLACE**: Place block on RFID pad
3. **React quickly** - you have limited time!
4. **Survive the round** - wrong actions or timeout eliminate you
5. **Last player standing wins!**

## 📁 Project Structure

```
minecraft-bop-it/
├── README.md                 # This file
├── central/                  # Central server code
│   ├── central.ino          # Main Arduino sketch
│   ├── Game/                # Game logic classes
│   ├── Player/              # Player management classes
│   └── Web/                 # Web interface files
└── block/                   # Player block code
    └── block.ino           # Player controller sketch
```