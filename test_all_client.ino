#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <MPU6050.h>
#include <math.h>


//button setup
const int buttonPin = 14; 
const int greenPin =5;
int buttonState = 0; 
int lastButtonState = 0;  // For debouncing

//shaker sensor setup
MPU6050 mpu;

//rfid reader setup
#define PN532_IRQ   (2)  //IRQ pin
#define PN532_RESET (3)  //RESET pin
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire1);

typedef struct struct_message {
  char msg[32];
} struct_message;

struct_message dataToSend;
struct_message incomingData;

// CHANGE THIS to your other ESP32’s MAC address
uint8_t peerAddress[] = {0x94, 0xe6, 0x86, 0x2b, 0xf5, 0x38} ;

//send callback (ESP-IDF v5)
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {

  status == ESP_NOW_SEND_SUCCESS;
  if (status==1)
  {
    Serial.println("Failed to send message");
  }
}

//receive callback (ESP-IDF v5)
void onReceive(const esp_now_recv_info *recv_info, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));

  Serial.println(incomingData.msg);
}

void blink_greenLed()
{
  digitalWrite(greenPin, HIGH);
  delay(500);  
  digitalWrite(greenPin, LOW);
}

void shake_detection()
{
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float accelX = ax / 16384.0;
  float accelY = ay / 16384.0;
  float accelZ = az / 16384.0;
  float magnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);\

  // Better shake detection: deviation from 1g
  float deviation = fabs(magnitude - 1.0);
  if (deviation > 0.4) {
    
    snprintf(dataToSend.msg, sizeof(dataToSend.msg), "Shake detected");
    esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&dataToSend, sizeof(dataToSend));
    blink_greenLed();
    if (result != ESP_OK) {
      Serial.println("Shake send failed!");
    }
    delay(500); // Debounce shake detection
  }
}

void rfid_detection()
{
  uint8_t uid[7];
  uint8_t uidLength;
  // The 4th parameter is timeout in milliseconds - CRITICAL for non-blocking behavior
  uint8_t success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50);

  if (success) {  
    
    snprintf(dataToSend.msg, sizeof(dataToSend.msg), "RFID found");
    esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&dataToSend, sizeof(dataToSend));
    blink_greenLed(); 
    if (result != ESP_OK) {
      Serial.println("RFID send failed!");
    }
    delay(1000); // Prevent repeated reads of same card
  }
}

void button_detection()
{
    //button detection
  buttonState = digitalRead(buttonPin);
  if (buttonState == LOW && lastButtonState == HIGH) {  // Button pressed (active LOW with pullup)
    delay(5);  // Debounce
    buttonState = digitalRead(buttonPin);
    if (buttonState == LOW) {  // Confirm press
      
      snprintf(dataToSend.msg, sizeof(dataToSend.msg), "Button pressed");
      esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&dataToSend, sizeof(dataToSend));
      blink_greenLed();
      if (result != ESP_OK) {
        Serial.println("Button send failed!");
      }
      // delay(10);  // Prevent rapid re-trigger
    }
  }
  lastButtonState = buttonState;
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);  //for esp32 now
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(greenPin, OUTPUT);
  Wire1.begin(18, 19);   // PN532 on I2C1   
  Wire.begin(21, 22);     // MPU6050 on I2C0

  mpu.initialize();   //shaker sensor setup
  nfc.begin();    //rfid reader setup
  nfc.SAMConfig();   //rfid reader setup
 
  // Updated callback registration
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void loop() {
  // button_detection();
  // shake_detection();
  // rfid_detection();
  rfid_detection();
  button_detection();
  shake_detection();
}
