// ESP32-S3 + two L298N modules + four DC motors.
// Local test only: forward -> coast stop -> reverse -> coast stop -> repeat.
// No Wi-Fi, no web server, no remote-control code.

struct MotorChannel {
  uint8_t in1;
  uint8_t in2;
  uint8_t enable;
};

// L298N #1
// Motor A: IN1=GPIO4, IN2=GPIO5, ENA=GPIO7
// Motor B: IN3=GPIO8, IN4=GPIO9, ENB=GPIO10
// L298N #2
// Motor A: IN1=GPIO11, IN2=GPIO12, ENA=GPIO13
// Motor B: IN3=GPIO14, IN4=GPIO15, ENB=GPIO16
// GPIO6 is deliberately not used.
const MotorChannel motors[] = {
  {4, 5, 7},
  {8, 9, 10},
  {11, 12, 13},
  {14, 15, 16},
};
constexpr size_t MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);

constexpr uint8_t TEST_PWM = 102;      // 40% of 255. Change only after testing.
constexpr uint32_t RUN_TIME_MS = 2000; // Each direction runs for 2 seconds.
constexpr uint32_t STOP_TIME_MS = 1000;

void coastStopAll()
{
  // EN low disables each bridge. This is a safe coast stop for testing.
  for (const MotorChannel& motor : motors) {
    analogWrite(motor.enable, 0);
    digitalWrite(motor.enable, LOW);
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, LOW);
  }
}

void driveAllForward()
{
  for (const MotorChannel& motor : motors) {
    digitalWrite(motor.in1, HIGH);
    digitalWrite(motor.in2, LOW);
    analogWrite(motor.enable, TEST_PWM);
  }
}

void driveAllReverse()
{
  for (const MotorChannel& motor : motors) {
    digitalWrite(motor.in1, LOW);
    digitalWrite(motor.in2, HIGH);
    analogWrite(motor.enable, TEST_PWM);
  }
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
  coastStopAll();
  Serial.println("Four-motor L298N cycle test started.");
}

void loop()
{
  Serial.println("All four motors: forward");
  driveAllForward();
  delay(RUN_TIME_MS);

  Serial.println("Stop");
  coastStopAll();
  delay(STOP_TIME_MS);

  Serial.println("All four motors: reverse");
  driveAllReverse();
  delay(RUN_TIME_MS);

  Serial.println("Stop");
  coastStopAll();
  delay(STOP_TIME_MS);
}
