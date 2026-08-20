#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uros_network_interfaces.h"

#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/int32.h>

// L298N wiring: IN1 -> GPIO4, IN2 -> GPIO5, ENA -> GPIO6.
#define MOTOR_IN1 GPIO_NUM_4
#define MOTOR_IN2 GPIO_NUM_5
#define MOTOR_ENA GPIO_NUM_6
#define MOTOR_LEDC_MODE LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_TIMER LEDC_TIMER_0
#define MOTOR_LEDC_CHANNEL LEDC_CHANNEL_0
#define MOTOR_PWM_FREQUENCY_HZ 20000
#define MOTOR_COMMAND_TIMEOUT_MS 300

static const char *TAG = "motor_controller";
static rcl_publisher_t status_publisher;
static int32_t applied_command = 0;
static int64_t last_command_time_us = 0;

static void set_pwm(uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CHANNEL));
}

static void brake_motor(void)
{
    // Both bridge inputs equal and ENA high dynamically brake the motor.
    gpio_set_level(MOTOR_IN1, 1);
    gpio_set_level(MOTOR_IN2, 1);
    set_pwm(255);
    applied_command = 0;
}

static void apply_motor_command(int32_t requested_command)
{
    if (requested_command > 255) {
        requested_command = 255;
    } else if (requested_command < -255) {
        requested_command = -255;
    }

    if (requested_command == 0) {
        brake_motor();
        return;
    }

    if (requested_command > 0) {
        gpio_set_level(MOTOR_IN1, 1);
        gpio_set_level(MOTOR_IN2, 0);
    } else {
        gpio_set_level(MOTOR_IN1, 0);
        gpio_set_level(MOTOR_IN2, 1);
    }

    set_pwm((uint32_t)abs(requested_command));
    applied_command = requested_command;
}

static void motor_gpio_init(void)
{
    const gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << MOTOR_IN1) | (1ULL << MOTOR_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output_config));

    const ledc_timer_config_t timer_config = {
        .speed_mode = MOTOR_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = MOTOR_LEDC_TIMER,
        .freq_hz = MOTOR_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    const ledc_channel_config_t channel_config = {
        .gpio_num = MOTOR_ENA,
        .speed_mode = MOTOR_LEDC_MODE,
        .channel = MOTOR_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_LEDC_TIMER,
        .duty = 255,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    brake_motor();
}

static void command_callback(const void *message_in)
{
    const std_msgs__msg__Int32 *command = message_in;
    apply_motor_command(command->data);
    last_command_time_us = esp_timer_get_time();
}

static void safety_timer_callback(rcl_timer_t *timer, int64_t last_call_time)
{
    (void)timer;
    (void)last_call_time;
    if (esp_timer_get_time() - last_command_time_us >
        (int64_t)MOTOR_COMMAND_TIMEOUT_MS * 1000) {
        brake_motor();
    }
}

static void status_timer_callback(rcl_timer_t *timer, int64_t last_call_time)
{
    (void)timer;
    (void)last_call_time;
    std_msgs__msg__Int32 status = {.data = applied_command};
    (void)rcl_publish(&status_publisher, &status, NULL);
}

static void check_or_stop(rcl_ret_t result, const char *operation)
{
    if (result != RCL_RET_OK) {
        ESP_LOGE(TAG, "%s failed: %d", operation, (int)result);
        brake_motor();
        vTaskDelete(NULL);
    }
}

static void micro_ros_task(void *argument)
{
    (void)argument;
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
    rclc_support_t support;
    rcl_node_t node;
    rcl_subscription_t command_subscription;
    rcl_timer_t safety_timer;
    rcl_timer_t status_timer;
    rclc_executor_t executor;
    std_msgs__msg__Int32 command_message;

    check_or_stop(rcl_init_options_init(&init_options, allocator), "init options");
    rmw_init_options_t *rmw_options = rcl_init_options_get_rmw_init_options(&init_options);
    check_or_stop(
        rmw_uros_options_set_udp_address(CONFIG_MICRO_ROS_AGENT_IP,
                                         CONFIG_MICRO_ROS_AGENT_PORT,
                                         rmw_options),
        "agent address");
    check_or_stop(rclc_support_init_with_options(&support, 0, NULL,
                                                  &init_options, &allocator),
                  "support init");
    check_or_stop(rclc_node_init_default(&node, "esp32_motor_controller", "", &support),
                  "node init");
    check_or_stop(rclc_subscription_init_default(
                      &command_subscription, &node,
                      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
                      "/motor/cmd"),
                  "command subscription");
    check_or_stop(rclc_publisher_init_default(
                      &status_publisher, &node,
                      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
                      "/motor/status"),
                  "status publisher");
    check_or_stop(rclc_timer_init_default(&safety_timer, &support,
                                           RCL_MS_TO_NS(50), safety_timer_callback),
                  "safety timer");
    check_or_stop(rclc_timer_init_default(&status_timer, &support,
                                           RCL_MS_TO_NS(100), status_timer_callback),
                  "status timer");
    check_or_stop(rclc_executor_init(&executor, &support.context, 3, &allocator),
                  "executor init");
    check_or_stop(rclc_executor_add_subscription(&executor, &command_subscription,
                                                  &command_message, command_callback,
                                                  ON_NEW_DATA),
                  "executor subscription");
    check_or_stop(rclc_executor_add_timer(&executor, &safety_timer), "executor safety timer");
    check_or_stop(rclc_executor_add_timer(&executor, &status_timer), "executor status timer");

    last_command_time_us = esp_timer_get_time();
    while (true) {
        (void)rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    motor_gpio_init();
    ESP_ERROR_CHECK(uros_network_interface_initialize());
    xTaskCreatePinnedToCore(micro_ros_task, "micro_ros_motor",
                            CONFIG_MOTOR_CONTROLLER_APP_STACK, NULL,
                            CONFIG_MOTOR_CONTROLLER_APP_TASK_PRIO, NULL, 1);
}
