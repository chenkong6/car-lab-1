/*
 * ESP32-S3 + 2 x L298N：单电机 Wi-Fi PWM 测试
 *
 * ESP32 会建立 2.4 GHz 热点：MotorPWM-Test
 * 密码：motorpwm123
 * 连接后，在浏览器打开： http://192.168.4.1
 *
 * 安全规则：任意时刻只允许一台电机转动；每次运行最多 5 秒，
 * 切换电机、方向或占空比前都会先停止全部电机。
 *
 * L298N 的 ENA/ENB 跳线帽必须拔掉。
 * 所有 L298N、电机电源、ESP32 的 GND 必须共地。
 */

#include <WiFi.h>
#include <WebServer.h>

constexpr char AP_SSID[] = "MotorPWM-Test";d:\A-work\电机\esp32s3_mecanum_wifi_demo\esp32s3_mecanum_wifi_demo.ino
constexpr char AP_PASSWORD[] = "motorpwm123";

constexpr uint32_t PWM_FREQ_HZ = 1000;  // L298N 电机测试用 1 kHz
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr uint32_t SAFETY_STOP_MS = 5000;

struct MotorChannel {
  uint8_t in1;
  uint8_t in2;
  uint8_t enable;
  const char* name;
};

// L298N #1: A(IN1=4, IN2=5, ENA=7), B(IN3=8, IN4=9, ENB=10)
// L298N #2: A(IN1=11, IN2=12, ENA=13), B(IN3=14, IN4=15, ENB=16)
const MotorChannel motors[] = {
  {4,  5,  7,  "L298N #1 / Motor A"},
  {8,  9,  10, "L298N #1 / Motor B"},
  {11, 12, 13, "L298N #2 / Motor A"},
  {14, 15, 16, "L298N #2 / Motor B"},
};
constexpr size_t MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);

WebServer server(80);
bool motorRunning = false;
uint32_t startedAt = 0;

void stopAllMotors() {
  for (const auto& motor : motors) {
    ledcWrite(motor.enable, 0);
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
  }
  motorRunning = false;
}

bool readIntArgument(const char* name, int& value) {
  if (!server.hasArg(name)) {
    return false;
  }
  value = server.arg(name).toInt();
  return true;
}

void handleRun() {
  int motorIndex;
  int dutyPercent;
  if (!readIntArgument("motor", motorIndex) || !readIntArgument("duty", dutyPercent) ||
      !server.hasArg("dir")) {
    server.send(400, "text/plain; charset=utf-8", "Missing motor, dir, or duty");
    return;
  }

  const String direction = server.arg("dir");
  if (motorIndex < 0 || motorIndex >= static_cast<int>(MOTOR_COUNT) ||
      dutyPercent < 0 || dutyPercent > 100 || (direction != "f" && direction != "r")) {
    server.send(400, "text/plain; charset=utf-8", "Invalid command");
    return;
  }

  stopAllMotors();
  const MotorChannel& motor = motors[motorIndex];
  const bool forward = direction == "f";
  const uint8_t duty = map(dutyPercent, 0, 100, 0, 255);

  digitalWrite(motor.in1, forward ? HIGH : LOW);
  digitalWrite(motor.in2, forward ? LOW : HIGH);
  ledcWrite(motor.enable, duty);

  motorRunning = dutyPercent > 0;
  startedAt = millis();
  Serial.printf("[RUN] %s, %s, %d%% (duty=%u)\n", motor.name,
                forward ? "forward" : "reverse", dutyPercent, duty);
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleStop() {
  stopAllMotors();
  Serial.println("[STOP] all motors");
  server.send(200, "text/plain; charset=utf-8", "Stopped");
}

void handleRoot() {
  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>电机 PWM 单路测试</title>
<style>
body{font-family:system-ui,sans-serif;max-width:520px;margin:28px auto;padding:0 16px;background:#f7f7f7;color:#222}
.card{background:white;padding:20px;border-radius:12px;box-shadow:0 2px 10px #0001}label{display:block;margin-top:15px;font-weight:600}
select,input,button{font:inherit;width:100%;box-sizing:border-box;padding:12px;margin-top:7px;border-radius:8px;border:1px solid #bbb}
button{border:0;color:white;font-weight:700;cursor:pointer}.forward{background:#147b37}.reverse{background:#a43131}.stop{background:#333;margin-top:14px}
#value{font-weight:700;font-size:1.3em}#status{min-height:1.4em;margin-top:16px;color:#333}
</style><body><div class="card"><h2>单电机 PWM 测试</h2>
<p>每次只运行一台；5 秒自动停止。</p>
<label>选择电机<select id="motor">
<option value="0">L298N #1 / Motor A</option><option value="1">L298N #1 / Motor B</option>
<option value="2">L298N #2 / Motor A</option><option value="3">L298N #2 / Motor B</option>
</select></label>
<label>PWM 占空比：<span id="value">0%</span><input id="duty" type="range" min="0" max="100" value="0" oninput="value.textContent=this.value+'%' "></label>
<button class="forward" onclick="run('f')">正转（最多 5 秒）</button>
<button class="reverse" onclick="run('r')">反转（最多 5 秒）</button>
<button class="stop" onclick="stopMotor()">立即停止</button><div id="status">就绪</div>
</div><script>
const statusBox=document.getElementById('status');
function run(dir){const m=document.getElementById('motor').value,d=document.getElementById('duty').value;
 fetch(`/run?motor=${m}&dir=${dir}&duty=${d}`).then(r=>r.text()).then(t=>statusBox.textContent=t==='OK'?`运行：${d}%（5 秒保护停机）`:t).catch(()=>statusBox.textContent='通信失败');}
function stopMotor(){fetch('/stop').then(()=>statusBox.textContent='已停止').catch(()=>statusBox.textContent='通信失败');}
</script></body></html>)HTML";
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (const auto& motor : motors) {
    pinMode(motor.in1, OUTPUT);
    pinMode(motor.in2, OUTPUT);
    ledcAttach(motor.enable, PWM_FREQ_HZ, PWM_RESOLUTION);
  }
  stopAllMotors();

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Failed to start Wi-Fi AP");
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/run", HTTP_GET, handleRun);
  server.on("/stop", HTTP_GET, handleStop);
  server.begin();

  Serial.println("Wi-Fi AP started");
  Serial.printf("SSID: %s\nOpen: http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();

  if (motorRunning && millis() - startedAt >= SAFETY_STOP_MS) {
    stopAllMotors();
    Serial.println("[SAFETY] 5-second timeout: all motors stopped");
  }
}
