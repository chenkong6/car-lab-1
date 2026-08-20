#include <WiFi.h>
#include <WebServer.h>

// DRV8833 two-wheel controller for ESP32-S3.
// DRV8833 module labels can be AIN1/AIN2/BIN1/BIN2 or simply IN1/IN2/IN3/IN4.
// OUT1/OUT2 connect to the left motor; OUT3/OUT4 connect to the right motor.
// SLP (nSLEEP) is actively controlled. Leave FLT/nFAULT unconnected.

constexpr uint8_t LEFT_IN1 = 4;   // DRV8833 IN1 / AIN1
constexpr uint8_t LEFT_IN2 = 5;   // DRV8833 IN2 / AIN2
constexpr uint8_t RIGHT_IN1 = 10; // DRV8833 IN3 / BIN1; GPIO6 is defective
constexpr uint8_t RIGHT_IN2 = 7;  // DRV8833 IN4 / BIN2
constexpr uint8_t SLEEP_PIN = 8;  // DRV8833 SLP / nSLEEP

constexpr char AP_SSID[] = "ESP32_DRV8833_2WD";
constexpr char AP_PASSWORD[] = "motor1234";
constexpr uint32_t CONTROL_TIMEOUT_MS = 1200;

WebServer server(80);

enum class DriveDirection { BRAKE, FORWARD, REVERSE };

DriveDirection direction = DriveDirection::BRAKE;
uint8_t speed_percent = 40;
uint32_t last_control_ms = 0;

const char HTML_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DRV8833 双轮控制</title><style>
:root{color-scheme:dark}body{max-width:560px;margin:auto;padding:26px 18px;text-align:center;font:20px system-ui;background:#12151a;color:#f4f7fb}h1{font-size:1.55rem}#state{min-height:1.6em;color:#a8d6ff;margin:12px 0 25px}.speed{display:grid;grid-template-columns:repeat(4,1fr);gap:9px;margin-bottom:27px}button{min-height:58px;border:0;border-radius:12px;color:#fff;background:#303946;font-size:1.1rem;font-weight:700;touch-action:manipulation}button.selected{outline:3px solid #f3c442;background:#4a5769}.drive{display:grid;gap:14px}#forward{background:#168842}#reverse{background:#176cc3}#brake{background:#bf3030}.note{margin-top:28px;color:#aeb7c3;font-size:.85rem;line-height:1.5}
</style></head><body>
<h1>DRV8833 双轮控制</h1><div id="state">正在读取状态…</div>
<div class="speed"><button data-speed="20">20%</button><button data-speed="40">40%</button><button data-speed="60">60%</button><button data-speed="80">80%</button></div>
<div class="drive"><button id="forward" onclick="move('forward')">▲ 两轮正转</button><button id="reverse" onclick="move('reverse')">▼ 两轮反转</button><button id="brake" onclick="move('brake')">■ 零值刹车</button></div>
<p class="note">运动时页面每 250 ms 保活；网页关闭、断开 Wi-Fi 或超时约 1.2 秒后自动刹车。</p>
<script>
let moving=false;async function request(path){const r=await fetch(path,{cache:'no-store'});const d=await r.json();document.querySelector('#state').textContent=`状态：${d.direction}　PWM：${d.speed}%`;document.querySelectorAll('[data-speed]').forEach(b=>b.classList.toggle('selected',+b.dataset.speed===d.speed));moving=d.direction!=='刹车'}
function move(v){request('/api/move?dir='+v).catch(offline)}function speed(v){request('/api/speed?value='+v).catch(offline)}function offline(){document.querySelector('#state').textContent='控制器连接失败，请检查 Wi-Fi'}document.querySelectorAll('[data-speed]').forEach(b=>b.onclick=()=>speed(b.dataset.speed));setInterval(()=>{if(moving)request('/api/keepalive').catch(offline)},250);request('/api/status').catch(offline);
</script></body></html>
)HTML";

uint8_t pwmDuty() {
  return static_cast<uint8_t>(255UL * speed_percent / 100UL);
}

const char* directionText() {
  if (direction == DriveDirection::FORWARD) return "正转";
  if (direction == DriveDirection::REVERSE) return "反转";
  return "刹车";
}

void writeDigital(uint8_t pin, uint8_t level) {
  // analogWrite() attaches ESP32 LEDC/PWM to the pin. Detach it before a
  // digital level is needed, otherwise a previous PWM waveform can survive
  // the stop/brake command and drive the motor in the opposite direction.
  ledcDetach(pin);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

void writePwm(uint8_t pin, uint8_t duty) {
  analogWriteFrequency(pin, 16000);  // Above audible range.
  analogWriteResolution(pin, 8);
  analogWrite(pin, duty);
}

void driveOneMotor(uint8_t in1, uint8_t in2) {
  if (direction == DriveDirection::FORWARD) {
    writePwm(in1, pwmDuty());
    writeDigital(in2, LOW);
  } else if (direction == DriveDirection::REVERSE) {
    writeDigital(in1, LOW);
    writePwm(in2, pwmDuty());
  } else {
    // Both inputs HIGH is DRV8833 fast decay / dynamic braking.
    writeDigital(in1, HIGH);
    writeDigital(in2, HIGH);
  }
}

void applyDrive() {
  // nSLEEP must be HIGH before the H-bridge can respond.
  digitalWrite(SLEEP_PIN, HIGH);
  driveOneMotor(LEFT_IN1, LEFT_IN2);
  driveOneMotor(RIGHT_IN1, RIGHT_IN2);
}

void disableDriver() {
  // Safe boot state: bridge disabled, both motors coast.
  digitalWrite(SLEEP_PIN, LOW);
  writeDigital(LEFT_IN1, LOW);
  writeDigital(LEFT_IN2, LOW);
  writeDigital(RIGHT_IN1, LOW);
  writeDigital(RIGHT_IN2, LOW);
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
  if (value == "forward") direction = DriveDirection::FORWARD;
  else if (value == "reverse") direction = DriveDirection::REVERSE;
  else if (value == "brake") direction = DriveDirection::BRAKE;
  else {
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
  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  pinMode(SLEEP_PIN, OUTPUT);
  disableDriver();

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
