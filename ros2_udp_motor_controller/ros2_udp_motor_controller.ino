#include <WiFi.h>
#include <WiFiUdp.h>

#include "secrets.h"

// L298N wiring: IN1 -> GPIO4, IN2 -> GPIO5, ENA -> GPIO6.
constexpr uint8_t MOTOR_IN1 = 4;
constexpr uint8_t MOTOR_IN2 = 5;
constexpr uint8_t MOTOR_ENA = 6;
constexpr uint16_t COMMAND_PORT = 8888;
constexpr uint16_t STATUS_PORT = 8889;
constexpr uint32_t COMMAND_TIMEOUT_MS = 300;
constexpr uint32_t STATUS_PERIOD_MS = 100;

WiFiUDP command_udp;
WiFiUDP status_udp;
int32_t applied_command = 0;
uint32_t last_command_time_ms = 0;
uint32_t last_status_time_ms = 0;
uint32_t last_wifi_attempt_ms = 0;
bool wifi_was_connected = false;

void set_motor_pwm(const uint8_t duty) {
  analogWrite(MOTOR_ENA, duty);
}

void brake_motor() {
  // Equal L298N input levels with ENA asserted dynamically brake the motor.
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, HIGH);
  set_motor_pwm(255);
  applied_command = 0;
}

void apply_motor_command(int32_t command) {
  command = constrain(command, -255, 255);
  if (command == 0) {
    brake_motor();
    return;
  }

  if (command > 0) {
    digitalWrite(MOTOR_IN1, HIGH);
    digitalWrite(MOTOR_IN2, LOW);
  } else {
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, HIGH);
  }

  set_motor_pwm(static_cast<uint8_t>(abs(command)));
  applied_command = command;
}

void send_status() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  char payload[24];
  snprintf(payload, sizeof(payload), "STATUS %ld\n", static_cast<long>(applied_command));
  status_udp.beginPacket(BRIDGE_IP, STATUS_PORT);
  status_udp.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
  status_udp.endPacket();
}

void handle_udp_command() {
  const int packet_size = command_udp.parsePacket();
  if (packet_size <= 0) {
    return;
  }

  char payload[32] = {0};
  const int bytes_read = command_udp.read(payload, sizeof(payload) - 1);
  if (bytes_read <= 0) {
    return;
  }

  long command = 0;
  if (sscanf(payload, "CMD %ld", &command) == 1) {
    apply_motor_command(command);
    last_command_time_ms = millis();
  }
}

void maintain_wifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    if (!wifi_was_connected) {
      Serial.print("WiFi connected. ESP32 IP: ");
      Serial.println(WiFi.localIP());
    }
    wifi_was_connected = true;
    return;
  }

  if (wifi_was_connected) {
    Serial.println("WiFi disconnected. Motor braked; reconnecting.");
  }
  wifi_was_connected = false;
  brake_motor();
  if (millis() - last_wifi_attempt_ms >= 5000) {
    last_wifi_attempt_ms = millis();
    Serial.println("Attempting WiFi connection...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_ENA, OUTPUT);
  brake_motor();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  command_udp.begin(COMMAND_PORT);
  Serial.println("Starting WiFi connection...");
  last_command_time_ms = millis();
}

void loop() {
  maintain_wifi();
  handle_udp_command();

  if (millis() - last_command_time_ms > COMMAND_TIMEOUT_MS) {
    brake_motor();
  }
  if (millis() - last_status_time_ms >= STATUS_PERIOD_MS) {
    last_status_time_ms = millis();
    send_status();
  }
}
