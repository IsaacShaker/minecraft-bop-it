// ================= Block.ino (ESP32) =================
// Individual block controller for Minecraft Bop-It game
// Handles sensor input, WebSocket communication, and game actions

// Core ESP32 libraries
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Arduino_JSON.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>

// Sensor libraries
#include <Adafruit_PN532.h>
#include <MPU6050.h>
#include <math.h>

// ======================== CONFIGURATION ========================

// Hardware pin assignments
#ifndef WIFI_STATUS_LED
#define WIFI_STATUS_LED 2
#endif

constexpr int PIN_LED_GREEN = 27;       // Power indicator LED
constexpr int PIN_ONBOARD_LED_BLUE = 2; // Connection status LED
constexpr int PIN_BUTTON = 14;          // Mine action button
constexpr int PIN_COMMAND_SPEAKER = 4;  // Audio command output
constexpr int PIN_LED_RED = 12;         // Error/action feedback LED
constexpr int PIN_LED_BLUE = 13;        // External Connection feedback LED

// Sensor configuration
constexpr int MPU6050_SDA = 21;         // Accelerometer I2C data
constexpr int MPU6050_SCL = 22;         // Accelerometer I2C clock
constexpr int PN532_SDA = 18;           // RFID I2C data
constexpr int PN532_SCL = 19;           // RFID I2C clock
constexpr int PN532_IRQ = 2;            // RFID interrupt pin
constexpr int PN532_RESET = 3;          // RFID reset pin

// Network configuration
const char* WIFI_SSID = "BlockParty";
const char* WIFI_PASS = "craft123";
const char* WS_HOST = "192.168.4.1";
const uint16_t WS_PORT = 80;
const char* WS_PATH = "/ws";

// Timing constants (milliseconds)
const uint32_t SYNC_PERIOD_MS = 2000;     // Status sync interval
const uint32_t DEBOUNCE_MS = 50;          // Button debounce time
const uint32_t SHAKE_DEBOUNCE_MS = 500;   // Shake detection debounce
const uint32_t RFID_TIMEOUT_MS = 50;      // RFID read timeout
const uint32_t RFID_DEBOUNCE_MS = 1000;   // RFID detection debounce
const uint32_t WIFI_TIMEOUT_MS = 8000;    // WiFi connection timeout
const float SHAKE_THRESHOLD = 0.4;        // Shake detection sensitivity

// ======================== GLOBAL INSTANCES ========================

// Sensor instances
MPU6050 mpu;
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire1);

// Storage and network
Preferences prefs;
WebSocketsClient ws;

// Device identification
String BLOCK_ID = "B-UNKNOWN"; // Will be loaded from NVS or generated

// ======================== STATE MANAGEMENT ========================

enum class State {
  BOOT,         // Initializing systems
  NET_CONNECT,  // Connecting to network
  REGISTERED,   // Connected to game server
  WAIT_ROUND,   // Waiting for round to start
  EXECUTING,    // Active round in progress
  REPORTED      // Round completed, waiting for next
};

State currentState = State::BOOT;

// ======================== GAME STATE VARIABLES ========================

// Current round information
String currentCmd = "";
int currentRound = 0;
int64_t roundStartServerMs = 0;   // When round officially starts (server time)
int64_t deadlineServerMs = 0;     // Round deadline (server time)
int64_t serverOffsetMs = 0;       // Time sync offset: serverTime - localTime
uint32_t gameTimeMs = 2000;       // Time allowed for player action

// Action tracking
bool actionDone = false;
uint32_t actionTimeMsLocal = 0;   // Local time when action completed
bool timeExpired = false;         // Set by timer interrupt
bool roundStarted = false;        // Whether round has begun

// Timing variables
uint32_t lastSyncStatusMs = 0;

// ======================== TIMER MANAGEMENT ========================

hw_timer_t* roundTimer = nullptr;

void IRAM_ATTR onRoundTimeout() {
  timeExpired = true;
}

void startRoundTimer(uint32_t timeoutMs) {
  if (roundTimer) {
    timerEnd(roundTimer);
  }
  timeExpired = false;
  roundTimer = timerBegin(1000000); // 1MHz frequency
  timerAttachInterrupt(roundTimer, &onRoundTimeout);
  timerAlarm(roundTimer, timeoutMs * 1000, false, 0); // Convert to microseconds
}

void stopRoundTimer() {
  if (roundTimer) {
    timerEnd(roundTimer);
    roundTimer = nullptr;
  }
  timeExpired = false;
}

// ======================== SENSOR MANAGEMENT ========================

/**
 * Initialize all sensors and I2C buses
 */
void initializeSensors() {
  // Initialize I2C bus for MPU6050 (accelerometer)
  Wire.begin(MPU6050_SDA, MPU6050_SCL);
  mpu.initialize();
  
  // Initialize I2C1 bus for PN532 (RFID)
  Wire1.begin(PN532_SDA, PN532_SCL);
  nfc.begin();
  nfc.SAMConfig();
}

bool detectMine() {
  Serial.println("detectMine function called");
  static uint32_t lastChangeTime = 0;
  static int lastReading = HIGH;
  static int stableState = HIGH;
  static bool buttonPressed = false;
  
  int reading = digitalRead(PIN_BUTTON);
  
  // Reset debounce timer on state change
  if (reading != lastReading) {
    lastChangeTime = millis();
  }
  
  // Check if reading has been stable long enough
  if ((millis() - lastChangeTime) > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;
      
      // Detect button press (transition to LOW)
      if (stableState == LOW && !buttonPressed) {
        buttonPressed = true;
        lastReading = reading;
        Serial.println("Button detected");
        return true; // Button was just pressed
      }
      
      // Detect button release (transition to HIGH)
      if (stableState == HIGH) {
        buttonPressed = false;
      }
    }
  }
  
  lastReading = reading;
  return false;
}

bool detectShake() {
  /// @todo: read accelerometer
  Serial.println("detectShake function called");

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float accelX = ax / 16384.0;
  float accelY = ay / 16384.0;
  float accelZ = az / 16384.0;
  float magnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);\

  // Better shake detection: deviation from 1g
  float deviation = fabs(magnitude - 1.0);
  if (deviation > 0.4) {
    //do something
    // delay(500); // Debounce shake detection
    Serial.println("Shake detected");
    // delay(500);
    return true;
  }
  
  return false;
}

bool detectPlace() {
  Serial.println("detectPlace function called");
  static uint32_t lastPlaceTime = 0;
  
  // Debounce RFID detection
  if (millis() - lastPlaceTime < RFID_DEBOUNCE_MS) {
    return false;
  }
  
  uint8_t uid[7];
  uint8_t uidLength;
  
  // Non-blocking RFID read with timeout
  uint8_t success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, RFID_TIMEOUT_MS);
  
  if (success) {
    lastPlaceTime = millis();
    Serial.println("RFID sticker detected");
    return true;
  }
  
  return false;
}

// ======================== AUDIO FEEDBACK ========================

void speakCommand(const String& cmd) {
  // TODO: Implement audio output based on command
  if (cmd == "SHAKE") {
    // Play "Shake-It!" audio
  } else if (cmd == "MINE") {
    // Play "Mine-It!" audio
  } else if (cmd == "PLACE") {
    // Play "Place-It!" audio
  }
  // Unknown commands are silently ignored
}

// ======================== TIME SYNCHRONIZATION ========================

int64_t nowServerMs() {
  return (int64_t)millis() + serverOffsetMs;
}

void updateServerOffset(int64_t serverTimeMs) {
  serverOffsetMs = serverTimeMs - (int64_t)millis();
}

// ======================== WEBSOCKET COMMUNICATION ========================

void wsSendJson(const JSONVar& doc) {
  String message = JSON.stringify(doc);
  ws.sendTXT(message);
}

void sendHello() {
  JSONVar doc;
  doc["type"] = "hello";
  doc["blockId"] = BLOCK_ID;
  wsSendJson(doc);
}

void sendStatus() {
  JSONVar doc;
  doc["type"] = "status";
  doc["blockId"] = BLOCK_ID;
  wsSendJson(doc);
}

void sendResult() {
  JSONVar doc;
  doc["type"] = "result";
  doc["blockId"] = BLOCK_ID;
  doc["round"] = currentRound;
  doc["actionDone"] = actionDone;
  wsSendJson(doc);
}

void handleRoundMessage(JSONVar& doc) {
  int newRound = doc.hasOwnProperty("round") ? (int)doc["round"] : 1;
  
  // Clean up any previous round
  stopRoundTimer();
  
  // Extract round parameters
  currentRound = newRound;
  currentCmd = doc.hasOwnProperty("cmd") ? (const char*)doc["cmd"] : "";
  Serial.print("Handle round message: ");
  Serial.println(currentCmd);
  roundStartServerMs = doc.hasOwnProperty("roundStartMs") ? (int64_t)(unsigned long)doc["roundStartMs"] : 0;
  deadlineServerMs = doc.hasOwnProperty("deadlineMs") ? (int64_t)(unsigned long)doc["deadlineMs"] : 0;
  gameTimeMs = doc.hasOwnProperty("gameTimeMs") ? (int)doc["gameTimeMs"] : 2000;
  
  // Reset round state
  actionDone = false;
  actionTimeMsLocal = 0;
  roundStarted = false;

  int64_t currentServerTime = nowServerMs();
  
  // Check if round has already expired
  if (currentServerTime >= deadlineServerMs) {
    sendResult();
    currentState = State::REPORTED;
    return;
  }

  // Check if round should start immediately
  if (currentServerTime >= roundStartServerMs) {
    startRoundNow();
  } else {
    currentState = State::WAIT_ROUND;
  }
}

void startRoundNow() {
  if (roundStarted) return; // Already started
  digitalWrite(PIN_LED_RED, LOW); // Clear previous feedback
  digitalWrite(PIN_LED_GREEN, LOW); // Clear previous feedback
  roundStarted = true;
  startRoundTimer(gameTimeMs);
  speakCommand(currentCmd);
  currentState = State::EXECUTING;
}

void handleWsMessage(const String& payload) {
  JSONVar doc = JSON.parse(payload);
  if (JSON.typeof(doc) == "undefined") {
    return; // Invalid JSON
  }

  String msgType = doc.hasOwnProperty("type") ? (const char*)doc["type"] : "";
  
  if (msgType == "sync") {
    int64_t serverTime = doc.hasOwnProperty("serverTimeMs") ? (int64_t)(unsigned long)doc["serverTimeMs"] : 0;
    updateServerOffset(serverTime);
  } else if (msgType == "round") {
    handleRoundMessage(doc);
  } else if (msgType == "control") {
    // TODO: Handle pause/resume/reset commands
  } else if (msgType == "assign") {
    // TODO: Handle name assignment
  }
  // Unknown message types are silently ignored
}

void wsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      digitalWrite(PIN_ONBOARD_LED_BLUE, HIGH);
      digitalWrite(PIN_LED_BLUE, HIGH);
      sendHello();
      currentState = State::REGISTERED;
      break;
      
    case WStype_DISCONNECTED:
      digitalWrite(PIN_ONBOARD_LED_BLUE, LOW);
      digitalWrite(PIN_LED_BLUE, LOW);
      stopRoundTimer();
      roundStarted = false;
      currentState = State::NET_CONNECT;
      break;
      
    case WStype_TEXT:
      handleWsMessage(String((char*)payload, len));
      break;
      
    default:
      // Other event types are ignored
      break;
  }
}

// ======================== NETWORK MANAGEMENT ========================

void connectWiFi() {
  pinMode(WIFI_STATUS_LED, OUTPUT);
  digitalWrite(WIFI_STATUS_LED, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  uint32_t startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(WIFI_STATUS_LED, HIGH);
  }
  else {
    digitalWrite(WIFI_STATUS_LED, LOW);
  }
}

// ======================== GAME LOGIC HANDLING ========================

void handleExecutingState() {
  // Check for timeout first
  if (timeExpired) {
    stopRoundTimer();
    digitalWrite(PIN_LED_RED, HIGH); // Indicate failure
    sendResult();
    currentState = State::REPORTED;
    return;
  }

  // Check for player action based on current command
  Serial.println(currentCmd);
  bool actionDetected = false;
  if (currentCmd == "MINE") {
    actionDetected = detectMine();
  } else if (currentCmd == "SHAKE") {
    actionDetected = detectShake();
  } else if (currentCmd == "PLACE") {
    actionDetected = detectPlace();
  }

  actionDetected = true;
  // Process successful action
  if (actionDetected && !actionDone) {
    // Stop timer first to prevent race condition
    stopRoundTimer();
    
    // Only count as success if not expired
    if (!timeExpired) {
      actionDone = true;
      actionTimeMsLocal = millis();
      digitalWrite(PIN_LED_GREEN, HIGH); // Visual feedback
      sendResult();
      currentState = State::REPORTED;
  }
}

// ======================== MAIN SETUP & LOOP ========================

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(100); // Allow serial to initialize

  // Configure GPIO pins
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_ONBOARD_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  // Set initial LED states
  digitalWrite(PIN_LED_GREEN, HIGH);  // success indicator
  digitalWrite(PIN_ONBOARD_LED_BLUE, LOW);    // Connection status
  digitalWrite(PIN_LED_RED, LOW);     // Error/action indicator
  digitalWrite(PIN_LED_BLUE, LOW);    // external connection status

  // Load or generate block ID from non-volatile storage
  prefs.begin("block");
  BLOCK_ID = prefs.getString("id", "");
  if (BLOCK_ID.length() == 0) {
    BLOCK_ID = "B" + String((uint32_t)esp_random() & 0xFFFF, HEX);
    prefs.putString("id", BLOCK_ID);
  }
  prefs.end();

  // Initialize sensors
  initializeSensors();

  // Connect to WiFi and WebSocket
  connectWiFi();
  ws.begin(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent([](WStype_t t, uint8_t* p, size_t l){ wsEvent(t,p,l); });
  ws.setReconnectInterval(1500);

  // Set initial state
  currentState = (WiFi.isConnected() ? State::NET_CONNECT : State::BOOT);
}

void loop() {
  //detectPlace();
  //Process WebSocket events
  ws.loop();

  // Send periodic status updates
  if (millis() - lastSyncStatusMs > SYNC_PERIOD_MS) {
    lastSyncStatusMs = millis();
    if (ws.isConnected()) {
      sendStatus();
    }
  }

  // Handle current game state
  switch (currentState) {
    case State::WAIT_ROUND:
      // Check if it's time to start the round
      if (nowServerMs() >= roundStartServerMs) {
        startRoundNow();
      }
      break;
    
    case State::EXECUTING:
      handleExecutingState();
      break;
    
    case State::REPORTED:
      // Wait for next round message
      break;

    case State::NET_CONNECT:
      if (!WiFi.isConnected()) {
        connectWiFi();
      }
      break;

    default:
      break;
  }
  
  // Small delay to prevent system overwhelm
  delay(1);
}
