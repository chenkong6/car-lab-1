// ESP32-S3 + two L298N modules: one-motor-at-a-time diagnostic.
// Use this when all four motors buzz but do not start together.

struct MotorChannel {
  uint8_t in1;
  uint8_t in2;
  uint8_t enable;
};

const MotorChannel motors[] = {
  {4, 5, 7},    // L298N #1, Motor A: IN1 IN2 ENA
  {8, 9, 10},   // L298N #1, Motor B: IN3 IN4 ENB
  {11, 12, 13}, // L298N #2, Motor A: IN1 IN2 ENA
  {14, 15, 16}, // L298N #2, Motor B: IN3 IN4 ENB
};
constexpr size_t MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);

constexpr uint8_t TEST_PWM = 102;       // 40%. Raise to 153 (60%) only if needed.
constexpr uint32_t RUN_TIME_MS = 800;
constexpr uint32_t STOP_TIME_MS = 800;

void stopAll()
{
  for (const MotorChannel& motor : motors) {
    analogWrite(motor.enable, 0);
    digitalWrite(motor.enable, LOW);
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
  }
}

void driveOne(size_t index, bool forward)
{
  stopAll();
  const MotorChannel& motor = motors[index];
  digitalWrite(motor.in1, forward ? HIGH : LOW);
  digitalWrite(motor.in2, forward ? LOW : HIGH);
  analogWrite(motor.enable, TEST_PWM);
}

void setup()
{
  Serial.begin(115200);
  for (const MotorChannel& motor : motors) {
    pinMode(motor.in1, OUTPUT);
    pinMode(motor.in2, OUTPUT);
    pinMode(motor.enable, OUTPUT);
    analogWriteFrequency(motor.enable, 12000);
  }
  stopAll();
  Serial.println("Sequential four-motor diagnostic started.");
}

void loop()
{
  for (size_t i = 0; i < MOTOR_COUNT; ++i) {
    Serial.printf("Motor %u forward\n", static_cast<unsigned>(i + 1));
    driveOne(i, true);
    delay(RUN_TIME_MS);
    stopAll();
    delay(STOP_TIME_MS);

    Serial.printf("Motor %u reverse\n", static_cast<unsigned>(i + 1));
    driveOne(i, false);
    delay(RUN_TIME_MS);
    stopAll();
    delay(STOP_TIME_MS);
  }
}
