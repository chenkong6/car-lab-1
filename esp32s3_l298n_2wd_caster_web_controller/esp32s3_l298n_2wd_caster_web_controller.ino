#include <WiFi.h>
#include <WebServer.h>

// Two rear driven wheels + one front caster wheel (differential drive).
// L298N: remove BOTH ENA and ENB jumpers before wiring the PWM pins.
// GPIO6 is known defective and is deliberately not used.

constexpr uint8_t LEFT_IN1 = 19;
constexpr uint8_t LEFT_IN2 = 20;
constexpr uint8_t LEFT_EN = 21;
constexpr uint8_t RIGHT_IN1 = 35;
constexpr uint8_t RIGHT_IN2 = 36;
constexpr uint8_t RIGHT_EN = 37;

constexpr char AP_SSID[] = "ESP32_2WD_Caster";
constexpr char AP_PASSWORD[] = "motor1234";
constexpr uint32_t CONTROL_TIMEOUT_MS = 1200;

WebServer server(80);

enum class Motion { BRAKE, FORWARD, REVERSE, TURN_LEFT, TURN_RIGHT };
Motion motion = Motion::BRAKE;
uint8_t speed_percent = 40;
uint32_t last_control_ms = 0;

const char HTML_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>两轮差速小车</title><style>
:root{color-scheme:dark}body{max-width:560px;margin:auto;padding:26px 18px;text-align:center;font:20px system-ui;background:#12151a;color:#f4f7fb}h1{font-size:1.55rem}#state{min-height:1.6em;color:#a8d6ff;margin:12px 0 25px}.speed{display:grid;grid-template-columns:repeat(4,1fr);gap:9px;margin-bottom:27px}button{min-height:58px;border:0;border-radius:12px;color:#fff;background:#303946;font-size:1.1rem;font-weight:700;touch-action:manipulation}button.selected{outline:3px solid #f3c442;background:#4a5769}.drive{display:grid;grid-template-columns:1fr 1fr;gap:14px}#forward,#reverse,#brake{grid-column:1/-1}#forward{background:#168842}#reverse{background:#176cc3}#left,#right{background:#7754b8}#brake{background:#bf3030}.note{margin-top:28px;color:#aeb7c3;font-size:.85rem;line-height:1.5}
</style></head><body>
<h1>两轮差速小车</h1><div id="state">正在读取状态…</div>
<div class="speed"><button data-speed="20">20%</button><button data-speed="40">40%</button><button data-speed="60">60%</button><button data-speed="80">80%</button></div>
<div class="drive"><button id="forward" onclick="move('forward')">▲ 前进</button><button id="left" onclick="move('left')">↺ 原地左旋</button><button id="right" onclick="move('right')">↻ 原地右旋</button><button id="reverse" onclick="move('reverse')">▼ 后退</button><button id="brake" onclick="move('brake')">■ 零值刹车</button></div>
<p class="note">两个后轮差速驱动，前轮为万向支撑轮。运动时网页每 250 ms 保活；关闭网页或断开 Wi-Fi 超过约 1.2 秒后自动刹车。</p>
<script>
let moving=false;async function request(path){const r=await fetch(path,{cache:'no-store'});const d=await r.json();document.querySelector('#state').textContent=`状态：${d.motion}　PWM：${d.speed}%`;document.querySelectorAll('[data-speed]').forEach(b=>b.classList.toggle('selected',+b.dataset.speed===d.speed));moving=d.motion!=='刹车'}
function move(v){request('/api/move?value='+v).catch(offline)}function speed(v){request('/api/speed?value='+v).catch(offline)}function offline(){document.querySelector('#state').textContent='控制器连接失败，请检查 Wi-Fi'}document.querySelectorAll('[data-speed]').forEach(b=>b.onclick=()=>speed(b.dataset.speed));setInterval(()=>{if(moving)request('/api/keepalive').catch(offline)},250);request('/api/status').catch(offline);
</script></body></html>
)HTML";

uint8_t pwmDuty() {
  return static_cast<uint8_t>(255UL * speed_percent / 100UL);
}

const char* motionText() {
  switch (motion) {
    case Motion::FORWARD: return "前进";
    case Motion::REVERSE: return "后退";
    case Motion::TURN_LEFT: return "原地左旋";
    case Motion::TURN_RIGHT: return "原地右旋";
    default: return "刹车";
  }
}

void setMotor(uint8_t in1, uint8_t in2, uint8_t enable, int8_t direction) {
  if (direction > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
    analogWrite(enable, pwmDuty());
  } else if (direction < 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
    analogWrite(enable, pwmDuty());
  } else {
    // L298N dynamic brake: equal inputs with EN asserted.
    digitalWrite(in1, HIGH);
    digitalWrite(in2, HIGH);
    analogWrite(enable, 255);
  }
}

void applyMotion() {
  int8_t left = 0;
  int8_t right = 0;
  if (motion == Motion::FORWARD) { left = 1; right = 1; }
  if (motion == Motion::REVERSE) { left = -1; right = -1; }
  if (motion == Motion::TURN_LEFT) { left = -1; right = 1; }
  if (motion == Motion::TURN_RIGHT) { left = 1; right = -1; }
  setMotor(LEFT_IN1, LEFT_IN2, LEFT_EN, left);
  setMotor(RIGHT_IN1, RIGHT_IN2, RIGHT_EN, right);
}

void safeBootState() {
  // Enable pins LOW while ESP32 starts: no motor output before a webpage command.
  analogWrite(LEFT_EN, 0);
  analogWrite(RIGHT_EN, 0);
  digitalWrite(LEFT_EN, LOW);
  digitalWrite(RIGHT_EN, LOW);
  digitalWrite(LEFT_IN1, LOW);
  digitalWrite(LEFT_IN2, LOW);
  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, LOW);
}

void sendStatus() {
  String json = "{\"motion\":\"";
  json += motionText();
  json += "\",\"speed\":";
  json += speed_percent;
  json += "}";
  server.send(200, "application/json", json);
}

void handleMove() {
  const String value = server.arg("value");
  if (value == "forward") motion = Motion::FORWARD;
  else if (value == "reverse") motion = Motion::REVERSE;
  else if (value == "left") motion = Motion::TURN_LEFT;
  else if (value == "right") motion = Motion::TURN_RIGHT;
  else if (value == "brake") motion = Motion::BRAKE;
  else { server.send(400, "application/json", "{\"error\":\"invalid motion\"}"); return; }
  last_control_ms = millis();
  applyMotion();
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
  applyMotion();
  sendStatus();
}

void handleKeepalive() { last_control_ms = millis(); sendStatus(); }

void setup() {
  Serial.begin(115200);
  pinMode(LEFT_IN1, OUTPUT); pinMode(LEFT_IN2, OUTPUT); pinMode(LEFT_EN, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT); pinMode(RIGHT_IN2, OUTPUT); pinMode(RIGHT_EN, OUTPUT);
  analogWriteFrequency(LEFT_EN, 12000);
  analogWriteFrequency(RIGHT_EN, 12000);
  safeBootState();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println();
  Serial.print("Hotspot SSID: "); Serial.println(AP_SSID);
  Serial.print("Control page: http://"); Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", HTML_PAGE); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/move", HTTP_GET, handleMove);
  server.on("/api/speed", HTTP_GET, handleSpeed);
  server.on("/api/keepalive", HTTP_GET, handleKeepalive);
  server.begin();
}

void loop() {
  server.handleClient();
  if (motion != Motion::BRAKE && millis() - last_control_ms > CONTROL_TIMEOUT_MS) {
    motion = Motion::BRAKE;
    applyMotion();
    Serial.println("Control timeout: motors braked.");
  }
}
