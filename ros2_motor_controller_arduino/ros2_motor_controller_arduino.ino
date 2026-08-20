#include <Arduino.h>
#include <micro_ros_arduino.h>
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <std_msgs/msg/int16.h>

#include "secrets.h"

// L298N wiring: IN1 -> GPIO4, IN2 -> GPIO5, ENA -> GPIO6.
constexpr uint8_t MOTOR_IN1 = 4;
constexpr uint8_t MOTOR_IN2 = 5;
constexpr uint8_t MOTOR_ENA = 6;
constexpr uint32_t COMMAND_TIMEOUT_MS = 300;

rcl_allocator_t allocator;
rclc_support_t support;
rcl_node_t node;
rcl_subscription_t command_subscription;
rcl_publisher_t status_publisher;
rcl_timer_t safety_timer;
rcl_timer_t status_timer;
rclc_executor_t executor;

std_msgs__msg__Int16 command_message;
std_msgs__msg__Int16 status_message;
int16_t applied_command = 0;
uint32_t last_command_time_ms = 0;

void brake_motor() {
  // Equal input levels with ENA asserted provide dynamic braking on the L298N.
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, HIGH);
  analogWrite(MOTOR_ENA, 255);
  applied_command = 0;
}

void apply_motor_command(const int16_t requested_command) {
  const int16_t command = constrain(requested_command, -255, 255);

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

  analogWrite(MOTOR_ENA, abs(command));
  applied_command = command;
}

void command_callback(const void *message_in) {
  const auto *received = static_cast<const std_msgs__msg__Int16 *>(message_in);
  apply_motor_command(received->data);
  last_command_time_ms = millis();
}

void safety_timer_callback(rcl_timer_t *, int64_t) {
  if (millis() - last_command_time_ms > COMMAND_TIMEOUT_MS) {
    brake_motor();
  }
}

void status_timer_callback(rcl_timer_t *, int64_t) {
  status_message.data = applied_command;
  rcl_publish(&status_publisher, &status_message, nullptr);
}

void halt_on_error() {
  brake_motor();
  while (true) {
    delay(1000);
  }
}

#define RCCHECK(fn)                  \
  do {                               \
    if ((fn) != RCL_RET_OK) {        \
      halt_on_error();               \
    }                                \
  } while (0)

void setup() {
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_ENA, OUTPUT);
  brake_motor();

  Serial.begin(115200);
  delay(1000);

  char wifi_ssid[] = WIFI_SSID;
  char wifi_password[] = WIFI_PASSWORD;
  IPAddress agent_ip(
      AGENT_IP_OCTET_1, AGENT_IP_OCTET_2,
      AGENT_IP_OCTET_3, AGENT_IP_OCTET_4);
  set_microros_wifi_transports(wifi_ssid, wifi_password, agent_ip, AGENT_PORT);
  delay(2000);

  allocator = rcl_get_default_allocator();
  RCCHECK(rclc_support_init(&support, 0, nullptr, &allocator));
  RCCHECK(rclc_node_init_default(&node, "esp32_motor_controller", "", &support));

  RCCHECK(rclc_subscription_init_default(
      &command_subscription,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int16),
      "/motor/cmd"));
  RCCHECK(rclc_publisher_init_default(
      &status_publisher,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int16),
      "/motor/status"));
  RCCHECK(rclc_timer_init_default(
      &safety_timer, &support, RCL_MS_TO_NS(50), safety_timer_callback));
  RCCHECK(rclc_timer_init_default(
      &status_timer, &support, RCL_MS_TO_NS(100), status_timer_callback));

  RCCHECK(rclc_executor_init(&executor, &support.context, 3, &allocator));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &command_subscription, &command_message,
      &command_callback, ON_NEW_DATA));
  RCCHECK(rclc_executor_add_timer(&executor, &safety_timer));
  RCCHECK(rclc_executor_add_timer(&executor, &status_timer));

  last_command_time_ms = millis();
}

void loop() {
  rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));
  delay(5);
}
