#include <WiFi.h>
#include <WebServer.h>

// ESP32-S3 + two L298N modules, driving four TT motors together.
// Remove BOTH ENA/ENB jumpers from each L298N module before wiring PWM pins.
// L298N #1: Motor A = GPIO4/5/6, Motor B = GPIO7/8/9.
// L298N #2: Motor A = GPIO10/11/12, Motor B = GPIO13/14/15.

struct MotorChannel {
  uint8_t in1;
  uint8_t in2;
  uint8_t enable;
};

const MotorChannel motors[] = {
  {4, 5, 6},
  {7, 8, 9},
  {10, 11, 12},
  {13, 14, 15},
};
constexpr size_t MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);

constexpr char AP_SSID[] = "ESP32_4WD_Motor";
constexpr char AP_PASSWORD[] = "motor1234";  // At least eight characters.
constexpr uint32_t CONTROL_TIMEOUT_MS = 1200;

WebServer server(80);

enum class DriveDirection { BRAKE, FORWARD, REVERSE };

DriveDirection direction = DriveDirection::BRAKE;
uint8_t speed_percent = 40;
uint32_t last_control_ms = 0;

const char HTML_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>四电机控制器</title>
  <style>
    :root { color-scheme: dark; }
    body { max-width: 560px; margin: 0 auto; padding: 26px 18px; font: 20px system-ui, sans-serif; background: #12151a; color: #f4f7fb; text-align: center; }
    h1 { font-size: 1.55rem; margin: 0 0 12px; }
    #state { color: #a8d6ff; min-height: 1.6em; margin: 0 0 26px; }
    .speed { display: grid; grid-template-columns: repeat(4, 1fr); gap: 9px; margin-bottom: 28px; }
    button { min-height: 58px; border: 0; border-radius: 12px; font-size: 1.1rem; font-weight: 700; color: white; background: #303946; touch-action: manipulation; }
    button.selected { outline: 3px solid #f3c442; background: #4a5769; }
    .drive { display: grid; gap: 14px; }
    #forward { background: #168842; } #reverse { background: #176cc3; } #brake { background: #bf3030; }
    .note { margin-top: 28px; font-size: .85rem; color: #aeb7c3; line-height: 1.5; }
  </style>
</head>
<body>
  <h1>ESP32-S3 四电机控制</h1>
  <div id="state">正在读取状态…</div>
  <div class="speed" id="speedButtons">
    <button data-speed="20">20%</button><button data-speed="40">40%</button>
    <button data-speed="60">60%</button><button data-speed="80">80%</button>
  </div>
  <div class="drive">
    <button id="forward" onclick="move('forward')">▲ 全部正转</button>
    <button id="reverse" onclick="move('reverse')">▼ 全部反转</button>
    <button id="brake" onclick="move('brake')">■ 零值刹车</button>
  </div>
  <p class="note">运行中页面每 250 ms 发送一次保活；手机断开、关页面或 Wi-Fi 失联约 1.2 秒后，电机自动刹车。</p>
  <script>
    let moving = false;
    async function request(path) {
      const response = await fetch(path, { cache: 'no-store' });
      const data = await response.json();
      document.querySelector('#state').textContent = `状态：${data.direction}　PWM：${data.speed}%`;
      document.querySelectorAll('[data-speed]').forEach(b => b.classList.toggle('selected', +b.dataset.speed === data.speed));
      moving = data.direction !== '刹车';
    }
    function move(value) { request('/api/move?dir=' + value).catch(showOffline); }
    function setSpeed(value) { request('/api/speed?value=' + value).catch(showOffline); }
    function showOffline() { document.querySelector('#state').textContent = '控制器连接失败，请检查 Wi-Fi'; }
    document.querySelectorAll('[data-speed]').forEach(b => b.onclick = () => setSpeed(b.dataset.speed));
    setInterval(() => { if (moving) request('/api/keepalive').catch(showOffline); }, 250);
    request('/api/status').catch(showOffline);
  </script>
</body>
</html>
)HTML";

uint8_t pwmDuty() {
  return static_cast<uint8_t>((255UL * speed_percent) / 100UL);
}

const char* directionText() {
  switch (direction) {
    case DriveDirection::FORWARD: return "正转";
    case DriveDirection::REVERSE: return "反转";
    default: return "刹车";
  }
}

void applyDrive() {
  for (const MotorChannel& motor : motors) {
    if (direction == DriveDirection::FORWARD) {
      digitalWrite(motor.in1, HIGH);
      digitalWrite(motor.in2, LOW);
      analogWrite(motor.enable, pwmDuty());
    } else if (direction == DriveDirection::REVERSE) {
      digitalWrite(motor.in1, LOW);
      digitalWrite(motor.in2, HIGH);
      analogWrite(motor.enable, pwmDuty());
    } else {
      // IN1 = IN2 = HIGH with EN asserted: dynamic (zero-value) braking.
      digitalWrite(motor.in1, HIGH);
      digitalWrite(motor.in2, HIGH);
      analogWrite(motor.enable, 255);
    }
  }
}

void disableAllOutputs() {
  // Safe software state: all H-bridge enable pins low, so each motor coasts.
  // This is used only while booting. Normal "brake" requests use applyDrive().
  for (const MotorChannel& motor : motors) {
    pinMode(motor.enable, OUTPUT);
    analogWrite(motor.enable, 0);
    digitalWrite(motor.enable, LOW);
  }
  for (const MotorChannel& motor : motors) {
    pinMode(motor.in1, OUTPUT);
    pinMode(motor.in2, OUTPUT);
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
  }
}

void sendStatus() {
  String json = "{\"direction\":\"";
  json += directionText();
  json += "\",\"speed\":";
  json += speed_percent;
  json += "}";
  server.send(200, "application/json", json);
}

void handleMove() {
  const String value = server.arg("dir");
  if (value == "forward") {
    direction = DriveDirection::FORWARD;
  } else if (value == "reverse") {
    direction = DriveDirection::REVERSE;
  } else if (value == "brake") {
    direction = DriveDirection::BRAKE;
  } else {
    server.send(400, "application/json", "{\"error\":\"invalid direction\"}");
    return;
  }
  last_control_ms = millis();
  applyDrive();
  sendStatus();
}

void handleSpeed() {
  const int value = server.arg("value").toInt();
  if (value != 20 && value != 40 && value != 60 && value != 80) {
    server.send(400, "application/json", "{\"error\":\"speed must be 20, 40, 60, or 80\"}");
    return;
  }
  speed_percent = static_cast<uint8_t>(value);
  last_control_ms = millis();
  applyDrive();
  sendStatus();
}

void handleKeepalive() {
  last_control_ms = millis();
  sendStatus();
}

void setup() {
  Serial.begin(115200);
  // Do not assert an L298N enable pin during boot. This removes any
  // firmware-controlled startup pulse; hardware pull-downs are still needed
  // to cover the earlier ESP32 reset/boot-ROM interval.
  disableAllOutputs();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println();
  Serial.print("Hotspot SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Control page: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", HTML_PAGE); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/move", HTTP_GET, handleMove);
  server.on("/api/speed", HTTP_GET, handleSpeed);
  server.on("/api/keepalive", HTTP_GET, handleKeepalive);
  server.begin();
}

void loop() {
  server.handleClient();
  if (direction != DriveDirection::BRAKE && millis() - last_control_ms > CONTROL_TIMEOUT_MS) {
    direction = DriveDirection::BRAKE;
    applyDrive();
    Serial.println("Control timeout: motors braked.");
  }
}
