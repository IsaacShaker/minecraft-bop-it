#include <WiFi.h>

const char* ssid = "iPhone";
const char* password = "ari1101$";
WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nServer started at IP: " + WiFi.localIP().toString());
  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    String msg = client.readStringUntil('\n');
    Serial.println("Received: " + msg);
    client.println("Hello from Server!");
  }
}

