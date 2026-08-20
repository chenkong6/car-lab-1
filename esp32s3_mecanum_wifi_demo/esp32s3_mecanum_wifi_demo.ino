/*
 * ESP32-S3 + 2 x L298N + 四轮麦克纳姆底盘：Wi-Fi 开环演示
 *
 * 热点：MotorMecanum
 * 密码：mecanum123
 * 浏览器： http://192.168.4.1
 *
 * 操作：按住网页方向键移动，松开立即停止。
 * 安全保护：超过 1.2 秒没有收到继续移动命令，全部电机自动停止。
 *
 * 接线与轮位：
 *   左前 FL = L298N #1 A：IN1=4,  IN2=5,  ENA=7
 *   右前 FR = L298N #1 B：IN3=8,  IN4=9,  ENB=10
 *   右后 RR = L298N #2 A：IN1=11, IN2=12, ENA=13
 *   左后 RL = L298N #2 B：IN3=14, IN4=15, ENB=16
 *
 * 两块 L298N 的 ENA/ENB 跳线帽必须拔掉；ESP32、L298N、电机电源必须共地。
 */

#include <WiFi.h>
#include <WebServer.h>

constexpr char AP_SSID[] = "MotorMecanum";
constexpr char AP_PASSWORD[] = "mecanum123";

constexpr uint32_t PWM_FREQ_HZ = 1000;
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1200;

// 已实测：左前 1A 的 38% 约等于其他三轮的 24%。
// 速度滑条的“基准 PWM”作用于其余三轮；左前自动乘以此补偿系数。
constexpr float FL_PWM_GAIN = 38.0f / 24.0f;
constexpr uint8_t MIN_BASE_PWM_PERCENT = 24;
constexpr uint8_t MAX_BASE_PWM_PERCENT = 63;  // 63% * 38/24 ~= 100%，防止左前饱和

enum Wheel : uint8_t { FL = 0, FR = 1, RR = 2, RL = 3, WHEEL_COUNT = 4 };

struct MotorChannel {
  uint8_t in1;
  uint8_t in2;
  uint8_t enable;
  const char* name;
};

const MotorChannel wheels[WHEEL_COUNT] = {
  {4,  5,  7,  "FL / 1A"},
  {8,  9,  10, "FR / 1B"},
  {11, 12, 13, "RR / 2A"},
  {14, 15, 16, "RL / 2B"},
};

// true 表示 IN1=HIGH、IN2=LOW 时，该轮沿“车辆前进方向”转。
// 若某个轮子让车辆动作错误，只修改对应的 true/false，不要改接线。
bool forwardIn1High[WHEEL_COUNT] = {true, true, true, true};

WebServer server(80);
bool moving = false;
uint32_t lastCommandAt = 0;

void stopAllMotors() {
  for (const auto& wheel : wheels) {
    ledcWrite(wheel.enable, 0);
    digitalWrite(wheel.in1, LOW);
    digitalWrite(wheel.in2, LOW);
  }
  moving = false;
}

uint8_t compensatedDuty(Wheel wheel, uint8_t basePercent) {
  float percent = basePercent;
  if (wheel == FL) {
    percent *= FL_PWM_GAIN;
  }
  percent = constrain(percent, 0.0f, 100.0f);
  return static_cast<uint8_t>((percent * 255.0f / 100.0f) + 0.5f);
}

// direction: +1 为车辆前进意义上的该轮正转；-1 为反转；0 为停止。
void setWheel(Wheel wheel, int8_t direction, uint8_t basePercent) {
  const MotorChannel& motor = wheels[wheel];
  if (direction == 0) {
    ledcWrite(motor.enable, 0);
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
    return;
  }

  const bool forward = direction > 0;
  const bool in1High = forward ? forwardIn1High[wheel] : !forwardIn1High[wheel];
  digitalWrite(motor.in1, in1High ? HIGH : LOW);
  digitalWrite(motor.in2, in1High ? LOW : HIGH);
  ledcWrite(motor.enable, compensatedDuty(wheel, basePercent));
}

void drive(const char* action, uint8_t basePercent) {
  // 顺序为 FL, FR, RR, RL。所有动作均针对标准 X 型麦克纳姆轮安装。
  int8_t direction[WHEEL_COUNT] = {0, 0, 0, 0};

  if (strcmp(action, "forward") == 0) {
    direction[FL] = +1; direction[FR] = +1; direction[RR] = +1; direction[RL] = +1;
  } else if (strcmp(action, "back") == 0) {
    direction[FL] = -1; direction[FR] = -1; direction[RR] = -1; direction[RL] = -1;
  } else if (strcmp(action, "left") == 0) {
    direction[FL] = -1; direction[FR] = +1; direction[RR] = -1; direction[RL] = +1;
  } else if (strcmp(action, "right") == 0) {
    direction[FL] = +1; direction[FR] = -1; direction[RR] = +1; direction[RL] = -1;
  } else if (strcmp(action, "rotate_left") == 0) {
    direction[FL] = -1; direction[FR] = +1; direction[RR] = +1; direction[RL] = -1;
  } else if (strcmp(action, "rotate_right") == 0) {
    direction[FL] = +1; direction[FR] = -1; direction[RR] = -1; direction[RL] = +1;
  } else {
    stopAllMotors();
    return;
  }

  stopAllMotors();  // 先关闭全部使能，再更新方向，避免切换瞬间反灌
  for (uint8_t i = 0; i < WHEEL_COUNT; ++i) {
    setWheel(static_cast<Wheel>(i), direction[i], basePercent);
  }
  moving = true;
  lastCommandAt = millis();
}

void handleMove() {
  if (!server.hasArg("action") || !server.hasArg("base")) {
    server.send(400, "text/plain; charset=utf-8", "Missing action or base");
    return;
  }

  const int requestedBase = server.arg("base").toInt();
  if (requestedBase < MIN_BASE_PWM_PERCENT || requestedBase > MAX_BASE_PWM_PERCENT) {
    server.send(400, "text/plain; charset=utf-8", "Base PWM out of range");
    return;
  }

  drive(server.arg("action").c_str(), static_cast<uint8_t>(requestedBase));
  Serial.printf("[MOVE] %s, base=%d%%, FL=%.1f%%\n", server.arg("action").c_str(),
                requestedBase, requestedBase * FL_PWM_GAIN);
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleStop() {
  stopAllMotors();
  Serial.println("[STOP]");
  server.send(200, "text/plain; charset=utf-8", "Stopped");
}

void handleRoot() {
  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>麦克纳姆底盘控制</title>
<style>
body{font-family:system-ui,sans-serif;max-width:440px;margin:20px auto;padding:0 14px;background:#f4f6f8;color:#18202a}
.card{background:#fff;padding:18px;border-radius:14px;box-shadow:0 3px 14px #0002}h2{margin:0 0 8px}p{color:#556;font-size:.9rem}
input{width:100%}.row{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;margin:10px 0}.btn{border:0;border-radius:10px;padding:16px 6px;font-size:1.35rem;font-weight:700;color:white;background:#1769aa;touch-action:none}.blank{visibility:hidden}.turn{background:#7256a8}.stop{width:100%;background:#b12626;border:0;border-radius:10px;padding:14px;color:#fff;font-size:1.1rem;font-weight:700}.note{font-size:.85rem;color:#566}#status{min-height:1.4em;font-weight:600}
</style><body><div class="card"><h2>麦克纳姆底盘</h2><p>按住方向键运行，松手停止。</p>
<label>基准 PWM（其余三轮）：<b id="pwm">30%</b><input id="base" type="range" min="24" max="63" value="30" oninput="pwm.textContent=this.value+'%' "></label>
<p class="note">左前 1A 自动补偿为约 <span id="fl">48%</span>。</p>
<div class="row"><button class="btn turn" data-a="rotate_left">↺</button><button class="btn" data-a="forward">↑</button><button class="btn turn" data-a="rotate_right">↻</button></div>
<div class="row"><span class="blank"></span><button class="btn" data-a="left">←</button><button class="btn" data-a="right">→</button></div>
<div class="row"><span class="blank"></span><button class="btn" data-a="back">↓</button><span class="blank"></span></div>
<button class="stop" id="stop">立即停止</button><p id="status">就绪</p></div>
<script>
const base=document.getElementById('base'),statusBox=document.getElementById('status');let timer=null;
function refreshInfo(){document.getElementById('fl').textContent=Math.min(100,base.value*38/24).toFixed(0)+'%'}base.addEventListener('input',refreshInfo);refreshInfo();
function send(action){fetch(`/move?action=${action}&base=${base.value}`).then(r=>r.text()).then(t=>statusBox.textContent=t==='OK'?`运行：${action}`:t).catch(()=>statusBox.textContent='通信失败')}
function clearTimer(){if(timer){clearInterval(timer);timer=null}}
function halt(){clearTimer();fetch('/stop').catch(()=>{});statusBox.textContent='已停止'}
document.querySelectorAll('[data-a]').forEach(b=>{const start=e=>{e.preventDefault();clearTimer();send(b.dataset.a);timer=setInterval(()=>send(b.dataset.a),400)};b.addEventListener('pointerdown',start);b.addEventListener('pointerup',halt);b.addEventListener('pointercancel',halt);b.addEventListener('pointerleave',e=>{if(e.buttons)halt()})});
document.getElementById('stop').addEventListener('click',halt);document.addEventListener('pointerup',halt);document.addEventListener('visibilitychange',()=>{if(document.hidden)halt()});
</script></body></html>)HTML";
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (const auto& wheel : wheels) {
    pinMode(wheel.in1, OUTPUT);
    pinMode(wheel.in2, OUTPUT);
    ledcAttach(wheel.enable, PWM_FREQ_HZ, PWM_RESOLUTION);
  }
  stopAllMotors();

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Failed to start Wi-Fi AP");
    return;
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/move", HTTP_GET, handleMove);
  server.on("/stop", HTTP_GET, handleStop);
  server.begin();

  Serial.printf("AP: %s\nOpen: http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();
  if (moving && millis() - lastCommandAt > COMMAND_TIMEOUT_MS) {
    stopAllMotors();
    Serial.println("[SAFETY] command timeout -> stopped");
  }
}
