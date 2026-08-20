/*
 * L298N EN 静态使能诊断（不使用 PWM）
 *
 * 目的：验证 ESP32 到 ENA / ENB 的接线和 GPIO 输出是否正常。
 * 请把两个 L298N 的 ENA、ENB 跳线帽全部拔掉；本程序会直接输出 HIGH，
 * 这等价于把 ENA/ENB 接到 3.3V。电机请悬空测试，并确保电机电源电压正确。
 */

struct MotorChannel {
  uint8_t in1;
  uint8_t in2;
  uint8_t enable;
  const char* name;
};

// L298N #1
// A: IN1=4, IN2=5, ENA=7
// B: IN3=8, IN4=9, ENB=10
// L298N #2
// A: IN1=11, IN2=12, ENA=13
// B: IN3=14, IN4=15, ENB=16
const MotorChannel motors[] = {
  {4,  5,  7,  "L298N#1 A"},
  {8,  9,  10, "L298N#1 B"},
  {11, 12, 13, "L298N#2 A"},
  {14, 15, 16, "L298N#2 B"},
};

constexpr size_t MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);
constexpr uint32_t RUN_TIME_MS = 300;
constexpr uint32_t STOP_TIME_MS = 800;

void stopAllMotors() {
  for (const auto& motor : motors) {
    digitalWrite(motor.enable, LOW);
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
  }
}

void runOneMotor(size_t index, bool forward) {
  stopAllMotors();

  const MotorChannel& motor = motors[index];
  digitalWrite(motor.in1, forward ? HIGH : LOW);
  digitalWrite(motor.in2, forward ? LOW : HIGH);
  digitalWrite(motor.enable, HIGH);  // 静态 3.3V 使能，非 PWM
}

void setup() {
  Serial.begin(115200);
  delay(500);

  for (const auto& motor : motors) {
    pinMode(motor.in1, OUTPUT);
    pinMode(motor.in2, OUTPUT);
    pinMode(motor.enable, OUTPUT);
  }
  stopAllMotors();
  Serial.println("L298N static-enable diagnostic started");
}

void loop() {
  for (size_t i = 0; i < MOTOR_COUNT; ++i) {
    Serial.printf("%s forward\n", motors[i].name);
    runOneMotor(i, true);
    delay(RUN_TIME_MS);
    stopAllMotors();
    delay(STOP_TIME_MS);

    Serial.printf("%s reverse\n", motors[i].name);
    runOneMotor(i, false);
    delay(RUN_TIME_MS);
    stopAllMotors();
    delay(STOP_TIME_MS);
  }
}
