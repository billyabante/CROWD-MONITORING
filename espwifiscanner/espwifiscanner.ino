#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Set Wi-Fi to station mode
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  delay(500);

  // Print the MAC address
  Serial.println();
  Serial.println("----------------------------------------");
  Serial.println("ESP32 Wi-Fi MAC Address Scanner");
  Serial.println("----------------------------------------");
  Serial.print("Station MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("----------------------------------------");
}

void loop() {
  // Nothing to do here
}