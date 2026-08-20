#include <ESP32Servo.h>

Servo servo;

const int servoPin = 5;

void setup() {
  servo.setPeriodHertz(50);              // 舵机标准频率：50Hz
  servo.attach(servoPin, 500, 2400);     // SG90 脉宽范围（微秒）
}

void loop() {
  servo.write(0);    // 转到 0°
  delay(1000);

  servo.write(90);   // 转到 90°
  delay(1000);

  servo.write(180);  // 转到 180°
  delay(1000);
}