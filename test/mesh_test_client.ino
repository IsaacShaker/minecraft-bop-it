#include <esp_now.h>
#include <WiFi.h>

typedef struct struct_message {
  char msg[32];
} struct_message;

struct_message dataToSend;
struct_message incomingData;

// CHANGE THIS to your other ESP32’s MAC address
uint8_t peerAddress[] = {0x94, 0xe6, 0x86, 0x2b, 0xf5, 0x38} ;

// ✅ New send callback (ESP-IDF v5)
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {

  status == ESP_NOW_SEND_SUCCESS;
  if (status==1)
  {
    Serial.println("Failed to send message");
  }
}

// ✅ New receive callback (ESP-IDF v5)
void onReceive(const esp_now_recv_info *recv_info, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));

  Serial.println(incomingData.msg);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

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

  Serial.println("Setup complete. Ready to communicate!");
}

void loop() {

  snprintf(dataToSend.msg, sizeof(dataToSend.msg), "Hello from client!");

  esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&dataToSend, sizeof(dataToSend));

  if (!result == ESP_OK) {
    Serial.println("Send failed!");
  }

  delay(2000);
}

