#include <WiFi.h>

const char* ssid = "iPhone";
const char* password = "ari1101$";
const char* serverIP = "172.20.10.12"; // Replace with server's IP

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
}

void loop() {
  WiFiClient client;
  if (client.connect(serverIP, 80)) {
    client.println("Hello, its me THE CLIENT!");
    String reply = client.readStringUntil('\n');
    Serial.println("Server says: " + reply);
    client.stop();
  }
  delay(2000);
}
