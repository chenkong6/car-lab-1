#include <WiFi.h>

// Thermal isolation test for ESP32-S3.
// It intentionally does NOT configure GPIO4/5/6/7/8/9/10 or any motor pin.
// Serial commands: i = Wi-Fi off, w = start AP, s = print current state.

constexpr char TEST_AP_SSID[] = "ESP32_Thermal_Test";
constexpr char TEST_AP_PASSWORD[] = "thermal88";
bool ap_running = false;

void stopWiFi() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  ap_running = false;
  Serial.println("[THERMAL] Wi-Fi OFF. Do not touch any motor GPIO.");
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(TEST_AP_SSID, TEST_AP_PASSWORD);
  ap_running = true;
  Serial.print("[THERMAL] AP ON: ");
  Serial.print(TEST_AP_SSID);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());
}

void printStatus() {
  Serial.print("[THERMAL] Wi-Fi mode: ");
  Serial.print(ap_running ? "AP ON" : "OFF");
  Serial.print("; free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  stopWiFi();
  Serial.println("[THERMAL] Stage A started. Wait exactly 2 minutes, then assess temperature.");
  Serial.println("[THERMAL] Send w for Stage B (AP ON); send i to turn it off; send s for state.");
}

void loop() {
  if (!Serial.available()) return;
  const char command = static_cast<char>(Serial.read());
  if (command == 'w' || command == 'W') startAP();
  if (command == 'i' || command == 'I') stopWiFi();
  if (command == 's' || command == 'S') printStatus();
}
