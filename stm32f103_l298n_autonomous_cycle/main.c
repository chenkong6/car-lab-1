/*
 * STM32F103C8T6 + L298N autonomous two-wheel differential-drive cycle.
 * Keil project requirement: select STM32F103C8Tx and include the CMSIS device
 * header/startup file so that stm32f10x.h and SystemCoreClock are available.
 *
 * Route (repeats forever): forward -> stop -> reverse -> stop -> left turn
 * -> stop -> right turn -> stop -> spin left -> stop -> spin right -> stop.
 */

#include "stm32f10x.h"

/* L298N wiring. Remove the ENA/ENB jumpers: PA0 and PA3 provide PWM. */
#define LEFT_EN_PIN       0U   /* PA0, TIM2_CH1 -> L298N ENA */
#define LEFT_IN1_PIN      1U   /* PA1 -> L298N IN1 */
#define LEFT_IN2_PIN      2U   /* PA2 -> L298N IN2 */
#define RIGHT_EN_PIN      3U   /* PA3, TIM2_CH4 -> L298N ENB */
#define RIGHT_IN1_PIN     4U   /* PA4 -> L298N IN3 */
#define RIGHT_IN2_PIN     5U   /* PA5 -> L298N IN4 */

#define PWM_PERCENT       40U  /* Change to 20, 40, 60, or 80 if desired. */
#define PWM_PERIOD        999U /* 1000 PWM counts */

volatile uint32_t g_ms = 0U;

void SysTick_Handler(void)
{
    g_ms++;
}

static void DelayMs(uint32_t duration_ms)
{
    const uint32_t start = g_ms;
    while ((g_ms - start) < duration_ms) {
        __WFI();
    }
}

static void SetPin(uint32_t pin)
{
    GPIOA->BSRR = (1UL << pin);
}

static void ClearPin(uint32_t pin)
{
    GPIOA->BRR = (1UL << pin);
}

static void SetPwm(uint8_t left_percent, uint8_t right_percent)
{
    TIM2->CCR1 = ((PWM_PERIOD + 1U) * left_percent) / 100U;
    TIM2->CCR4 = ((PWM_PERIOD + 1U) * right_percent) / 100U;
}

/* direction: +1 forward, -1 reverse, 0 coast/stop */
static void SetLeftMotor(int8_t direction)
{
    if (direction > 0) {
        SetPin(LEFT_IN1_PIN);
        ClearPin(LEFT_IN2_PIN);
    } else if (direction < 0) {
        ClearPin(LEFT_IN1_PIN);
        SetPin(LEFT_IN2_PIN);
    } else {
        ClearPin(LEFT_IN1_PIN);
        ClearPin(LEFT_IN2_PIN);
    }
}

static void SetRightMotor(int8_t direction)
{
    if (direction > 0) {
        SetPin(RIGHT_IN1_PIN);
        ClearPin(RIGHT_IN2_PIN);
    } else if (direction < 0) {
        ClearPin(RIGHT_IN1_PIN);
        SetPin(RIGHT_IN2_PIN);
    } else {
        ClearPin(RIGHT_IN1_PIN);
        ClearPin(RIGHT_IN2_PIN);
    }
}

static void Stop(void)
{
    /* Coast stop: disable ENA/ENB before changing direction. */
    SetPwm(0U, 0U);
    SetLeftMotor(0);
    SetRightMotor(0);
}

static void Forward(void)
{
    SetLeftMotor(1);
    SetRightMotor(1);
    SetPwm(PWM_PERCENT, PWM_PERCENT);
}

static void Reverse(void)
{
    SetLeftMotor(-1);
    SetRightMotor(-1);
    SetPwm(PWM_PERCENT, PWM_PERCENT);
}

static void TurnLeft(void)
{
    /* Gentle left turn: left rear wheel stops, right rear wheel advances. */
    SetLeftMotor(0);
    SetRightMotor(1);
    SetPwm(0U, PWM_PERCENT);
}

static void TurnRight(void)
{
    /* Gentle right turn: right rear wheel stops, left rear wheel advances. */
    SetLeftMotor(1);
    SetRightMotor(0);
    SetPwm(PWM_PERCENT, 0U);
}

static void SpinLeft(void)
{
    SetLeftMotor(-1);
    SetRightMotor(1);
    SetPwm(PWM_PERCENT, PWM_PERCENT);
}

static void SpinRight(void)
{
    SetLeftMotor(1);
    SetRightMotor(-1);
    SetPwm(PWM_PERCENT, PWM_PERCENT);
}

static void GPIO_PWM_Init(void)
{
    SystemCoreClockUpdate();

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /* PA0/PA3: alternate-function push-pull, 50 MHz. PA1/2/4/5: GPIO output. */
    GPIOA->CRL &= ~((0xFUL << 0)  | (0xFUL << 4)  | (0xFUL << 8) |
                     (0xFUL << 12) | (0xFUL << 16) | (0xFUL << 20));
    GPIOA->CRL |=  ((0xBUL << 0)  | /* PA0 AF push-pull */
                    (0x3UL << 4)  | /* PA1 push-pull */
                    (0x3UL << 8)  | /* PA2 push-pull */
                    (0xBUL << 12) | /* PA3 AF push-pull */
                    (0x3UL << 16) | /* PA4 push-pull */
                    (0x3UL << 20));/* PA5 push-pull */

    /* TIM2 is configured for approximately 1 kHz PWM. */
    TIM2->PSC = (SystemCoreClock / 1000000U) - 1U;
    TIM2->ARR = PWM_PERIOD;
    TIM2->CCR1 = 0U;
    TIM2->CCR4 = 0U;
    TIM2->CCMR1 = (6UL << 4) | TIM_CCMR1_OC1PE;       /* CH1 PWM mode 1 */
    TIM2->CCMR2 = (6UL << 12) | TIM_CCMR2_OC4PE;      /* CH4 PWM mode 1 */
    TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC4E;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    Stop();
    SysTick_Config(SystemCoreClock / 1000U);
}

int main(void)
{
    GPIO_PWM_Init();

    while (1) {
        Forward();    DelayMs(3000U);
        Stop();       DelayMs(1000U);

        Reverse();    DelayMs(2000U);
        Stop();       DelayMs(1000U);

        TurnLeft();   DelayMs(1200U);
        Stop();       DelayMs(1000U);

        TurnRight();  DelayMs(1200U);
        Stop();       DelayMs(1000U);

        SpinLeft();   DelayMs(1000U);
        Stop();       DelayMs(1000U);

        SpinRight();  DelayMs(1000U);
        Stop();       DelayMs(2000U);
    }
}
