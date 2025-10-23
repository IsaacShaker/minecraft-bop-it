// ================= Block.ino (ESP32) =================
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Arduino_JSON.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>

#include <Adafruit_PN532.h>
#include <MPU6050.h>
#include <math.h>

// WIFI Status LED
#ifndef WIFI_STATUS_LED
#define WIFI_STATUS_LED 2
#endif

// -------- Pins (assign to right pins when we prototype) --------
constexpr int PIN_LED_GREEN       = 5;  // Power indicator
constexpr int PIN_LED_BLUE        = 2;  // Synced indicator //TBD
constexpr int PIN_BUTTON          = 14;  // "Mine it" button
constexpr int PIN_COMMAND_SPEAKER = 4;  // command speaker pin    //TBD
//TODO: ADD red led 

// RFID pins //do not need this setup
constexpr int PIN_RFID_SDA  = 5;
constexpr int PIN_RFID_SCK  = 6;
constexpr int PIN_RFID_MOSI = 7;
constexpr int PIN_RFID_MISO = 8;
constexpr int PIN_RFID_RST  = 9;

//SHAKER SENSOR SETUP
MPU6050 mpu;

//RFID SETUP
#define PN532_IRQ   (2)  //IRQ pin
#define PN532_RESET (3)  //RESET pin
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire1);

// -------- Network config --------
const char* WIFI_SSID = "BlockParty";
const char* WIFI_PASS = "craft123";
const char* WS_HOST   = "192.168.4.1";
const uint16_t WS_PORT = 80;
const char* WS_PATH   = "/ws";

// -------- IDs / persistence --------
Preferences prefs;
String BLOCK_ID = "B-UNKNOWN"; // Will be loaded from Non-Volatile Storage or generated

// -------- State machine --------
enum class State { OFF, BOOT, NET_CONNECT, REGISTERED, WAIT_ROUND, EXECUTING, REPORTED };
State state = State::OFF;

// -------- Round Variables --------
String currentCmd = "";
int    currentRound = -1;
int64_t roundStartServerMs = 0; // when round officially starts (server time)
int64_t deadlineServerMs = 0;   // absolute server time (ms)
int64_t serverOffsetMs   = 0;   // serverTimeMs - millis() to account for communication delay
uint32_t gameTimeMs = 2000;     // how long player has to respond

bool actionDone = false;
uint32_t actionTimeMsLocal = 0; // millis() when completed
bool timeExpired = false;       // set by timer interrupt
bool roundStarted = false;      // whether we've started the actual game round

// -------- Timers --------
uint32_t lastSyncStatusMs = 0;
const uint32_t SYNC_PERIOD_MS = 2000;

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
  timerAlarm(roundTimer, timeoutMs * 1000, false, 0); // Convert ms to microseconds, no reload
}

void stopRoundTimer() {
  if (roundTimer) {
    timerEnd(roundTimer);
    roundTimer = nullptr;
  }
  timeExpired = false;
}

// -------- Sensors --------
void sensorsInit() {
  Wire.begin();
  // pinMode(buttonPin, INPUT_PULLUP);
  // pinMode(greenPin, OUTPUT);
  
  /// @todo accelerometer init
  Wire.begin(21, 22);     // MPU6050 on I2C0
  mpu.initialize();   //shaker sensor setup
  
  /// @todo RFID init
  Wire1.begin(18, 19);   // PN532 on I2C1  
  nfc.begin();    //rfid reader setup
  nfc.SAMConfig();   //rfid reader setup
}

bool detectMine() {
  // Button with debounce
  static uint32_t lastChangeTime = 0;
  static int lastReading = HIGH;
  static int stableState = HIGH;
  static bool buttonPressed = false;
  
  int reading = 0; //digitalRead(PIN_BUTTON);
  
  // If reading changed, reset debounce timer
  if (reading != lastReading) {
    lastChangeTime = millis();
  }
  
  // If reading has been stable for debounce period
  if ((millis() - lastChangeTime) > 50) {
    // If state actually changed
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW && !buttonPressed) {
        buttonPressed = true;
        lastReading = reading;
        return true; // Button was just pressed
      }
      if (stableState == HIGH) {
        buttonPressed = false; // Button released
      }
    }
  }
  
  lastReading = reading;
  return false;
}

bool detectShake() {
  /// @todo: read accelerometer
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
    delay(500); // Debounce shake detection
  }
  
  return false;
}

bool detectPlace() {
  /// @todo: read RFID
  uint8_t uid[7];
  uint8_t uidLength;
  // The 4th parameter is timeout in milliseconds - CRITICAL for non-blocking behavior
  uint8_t success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50);
  
 //if (sucess)  
// {  do something
//     delay(1000); // Prevent repeated reads of same card *THIS IS NEEDED
//    }
  return false;
}

// -------- Speaker --------
void speakCommand(const String& cmd) {
  if (cmd == "SHAKE") { /** @todo implement voice for shake command "Shake-It!" */ }
  else if (cmd == "MINE") { /** @todo implement voice for mine command "Mine-It!" */ }
  else if (cmd == "PLACE") { /** @todo implement voice for place command "Place-It!" */ }
  else {
    /// @todo possibly log error so we can debug?
  }

}

// -------- Utilities --------
int64_t nowServerMs() {
  return (int64_t)millis() + serverOffsetMs;
}

void updateServerOffset(int64_t serverTimeMs) {
  serverOffsetMs = serverTimeMs - (int64_t)millis();
}

// -------- WebSocket (client) --------
WebSocketsClient ws;

void wsSendJson(const JSONVar& doc) {
  String out = JSON.stringify(doc);
  ws.sendTXT(out);
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
  int newRound = doc.hasOwnProperty("round") ? (int)doc["round"] : -1;
  
  // Ignore duplicate or old round messages
  if (newRound <= currentRound && currentRound != -1) {
    return;
  }
  
  // Clean up any previous round
  stopRoundTimer();
  
  currentRound = newRound;
  currentCmd   = doc.hasOwnProperty("cmd") ? (const char*)doc["cmd"] : "";
  roundStartServerMs = doc.hasOwnProperty("roundStartMs") ? (int64_t)(unsigned long)doc["roundStartMs"] : 0;
  deadlineServerMs = doc.hasOwnProperty("deadlineMs") ? (int64_t)(unsigned long)doc["deadlineMs"] : 0;
  gameTimeMs = doc.hasOwnProperty("gameTimeMs") ? (int)doc["gameTimeMs"] : 2000;
  actionDone = false;
  actionTimeMsLocal = 0;
  roundStarted = false;

  int64_t currentServerTime = nowServerMs();
  if (currentServerTime >= deadlineServerMs) {
    // Too late, fail immediately
    sendResult();
    state = State::REPORTED;
    return;
  }

  if (currentServerTime >= roundStartServerMs) {
    // Round already started, begin immediately
    startRoundNow();
  } else {
    // Wait for official start time
    state = State::WAIT_ROUND;
  }
}

void startRoundNow() {
  if (roundStarted) return; // Already started
  
  roundStarted = true;
  startRoundTimer(gameTimeMs);
  speakCommand(currentCmd);
  state = State::EXECUTING;
}

void handleWsMessage(const String& payload) {
  JSONVar doc = JSON.parse(payload);
  if (JSON.typeof(doc) == "undefined") return;

  const char* type = doc.hasOwnProperty("type") ? (const char*)doc["type"] : "";
  if (!strcmp(type, "sync")) {
    int64_t serverTime = doc.hasOwnProperty("serverTimeMs") ? (int64_t)(unsigned long)doc["serverTimeMs"] : 0;
    updateServerOffset(serverTime);
  } else if (!strcmp(type, "round")) {
    handleRoundMessage(doc);
  } else if (!strcmp(type, "control")) {
    /// @todo: pause/resume/reset if needed (potential stretch goal)
  } else if (!strcmp(type, "assign")) {
    /// @todo: optional name assignment (potential stretch goal)
  } else {
    // Unknown type;
    /// @todo possibly log error so we can debug?
  }
}

void wsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      // digitalWrite(PIN_LED_BLUE, HIGH);
      sendHello();
      /// @todo wait for ack from server?
      state = State::REGISTERED;
      break;
    case WStype_DISCONNECTED:
      // digitalWrite(PIN_LED_BLUE, LOW);
      // Clean up any active round timer
      stopRoundTimer();
      roundStarted = false;
      state = State::NET_CONNECT;
      break;
    case WStype_TEXT:
      handleWsMessage(String((char*)payload, len));
      break;
    default: break;
  }
}

// -------- Setup / Loop --------
void connectWiFi() {
  // WiFi Status LED (HIGH = Connected)
  pinMode(WIFI_STATUS_LED, OUTPUT);
  digitalWrite(WIFI_STATUS_LED, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
    Serial.print(".");
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" WiFi connected");
    digitalWrite(WIFI_STATUS_LED, HIGH); // solid ON = WiFi connected
  } else {
    Serial.println(" WiFi connection timeout");
  }
}

void setup() {
  // Setup Serial
  Serial.begin(115200);

  // Set pin modes
  // pinMode(PIN_LED_GREEN, OUTPUT);
  // pinMode(PIN_LED_BLUE, OUTPUT);
  // pinMode(PIN_BUTTON, INPUT_PULLUP);
  // digitalWrite(PIN_LED_GREEN, HIGH); // power on
  // digitalWrite(PIN_LED_BLUE, LOW);

  // Load block ID form non-volatile storage (NVS) or generate new
  prefs.begin("block");
  BLOCK_ID = prefs.getString("id", "");
  if (BLOCK_ID.length() == 0) {
    BLOCK_ID = "B" + String((uint32_t)esp_random() & 0xFFFF, HEX);
    prefs.putString("id", BLOCK_ID);
  }
  prefs.end();

  sensorsInit();

  connectWiFi();
  ws.begin(WS_HOST, WS_PORT, WS_PATH);
  // register event handler
  ws.onEvent([](WStype_t t, uint8_t* p, size_t l){ wsEvent(t,p,l); });
  ws.setReconnectInterval(1500);

  state = (WiFi.isConnected() ? State::NET_CONNECT : State::BOOT);
}

void loop() {
  ws.loop();

  // sync status
  if (millis() - lastSyncStatusMs > SYNC_PERIOD_MS) {
    lastSyncStatusMs = millis();
    if (ws.isConnected()) sendStatus();
  }

  switch (state) {
    case State::WAIT_ROUND: {
      // Check if it's time to start the round
      if (nowServerMs() >= roundStartServerMs) {
        startRoundNow();
      }
      break;
    }
    
    case State::EXECUTING: {
      // Check for timeout first
      if (timeExpired) {
        stopRoundTimer();
        sendResult();
        state = State::REPORTED;
        break;
      }

      // Check action
      bool hit = false;
      if (currentCmd == "MINE")  hit = detectMine();
      else if (currentCmd == "SHAKE") hit = detectShake();
      else if (currentCmd == "PLACE") hit = detectPlace();

      // Record time of action completion - only if not expired
      if (hit && !actionDone) {
        // Stop timer first to prevent race condition
        stopRoundTimer();
        if (!timeExpired) {
          actionDone = true;
          actionTimeMsLocal = millis();
          sendResult();
          state = State::REPORTED;
        }
        // If timeExpired is now true, the timeout handler will take care of it
      }
      break;
    }
    
    case State::REPORTED:
      // Stay in REPORTED state until new round message arrives
      // (handleRoundMessage will transition us to WAIT_ROUND or EXECUTING)
      break;

    case State::NET_CONNECT:
      if (!WiFi.isConnected()) connectWiFi();
      // ws library will reconnect automatically
      break;

    default: break;
  }
}
