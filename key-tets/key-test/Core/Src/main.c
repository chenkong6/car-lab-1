/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_customhid.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define STEPS_PER_REV   1600        /* TB6600: 8 microstep, 200 * 8 */
#define MM_PER_REV      60          /* 30T x 2GT, 2 mm pitch */
#define STEP_PULSE_HZ   1000U       /* PUL frequency */
#define MAX_TRAVEL_UM   300000      /* 300 mm */
#define MAX_TRAVEL_STEPS ((MAX_TRAVEL_UM * STEPS_PER_REV) / (MM_PER_REV * 1000))

#define ERR_NONE        0U
#define ERR_NOT_HOMED   1U
#define ERR_LIMIT       2U
#define ERR_HOME_FAIL   3U
#define JOG_MAX_UM      5000        /* manual jog max 5 mm per command */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t  key_stable    = 0U;
static uint8_t  key_last_raw  = 0U;
static uint8_t  key_dirty     = 1U;
static uint32_t key_change_ms = 0U;
static uint32_t telemetry_ms  = 0U;

static TIM_HandleTypeDef htim3;
static volatile int32_t  s_pos_steps  = 0;
static volatile uint32_t s_steps_left = 0U;
static volatile int8_t   s_dir        = 1;
static volatile uint8_t  s_moving     = 0U;
static volatile uint8_t  s_homed      = 0U;
static volatile uint8_t  s_homing     = 0U;
static volatile uint8_t  s_key_hit    = 0U;
static volatile uint8_t  s_error      = ERR_NONE;

volatile int32_t g_hid_target_um = 0;
volatile uint8_t g_hid_move_req  = 0U;
volatile uint8_t g_hid_reset_req = 0U;
volatile int32_t g_hid_jog_um    = 0;
volatile uint8_t g_hid_jog_req   = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Stepper_Init(void);
static void Stepper_MoveTo(int32_t target_um);
static void Stepper_Jog(int32_t delta_um);
static void Home_Start(void);
static void Stepper_Stop(void);
static void Stepper_Run(uint32_t steps, int8_t dir);
static void Zero_Set(void);

/**
  * @brief  Send the 7-byte input report: [1][throttle][X_L][X_H][Y_L][Y_H][buttons]
  */
static uint8_t Key_SendReport(uint8_t pressed)
{
  uint8_t report[7] = {1U, 0U, 0U, 0U, 0U, 0U, 0U};

  report[6] = (pressed != 0U) ? 0x00U : 0x01U;   /* 0 = pressed, 1 = released */
  return USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, report, (uint16_t)sizeof(report));
}

/**
  * @brief  Input report 2: [2][int32 position in 0.001 mm][status][error]
  *         status: bit0 moving, bit1 key, bit2 at zero, bit3 homed, bit4 error
  */
static uint8_t Telemetry_Send(void)
{
  uint8_t report[7];
  int32_t pos_um;
  uint8_t status;

  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
  {
    return USBD_FAIL;
  }

  pos_um = (int32_t)(((int64_t)s_pos_steps * (MM_PER_REV * 1000)) / STEPS_PER_REV);
  status = (uint8_t)((s_moving ? 0x01U : 0U) |
                     (key_stable ? 0x02U : 0U) |
                     ((s_pos_steps == 0) ? 0x04U : 0U) |
                     (s_homed ? 0x08U : 0U) |
                     ((s_error != ERR_NONE) ? 0x10U : 0U));

  report[0] = 2U;
  report[1] = (uint8_t)(pos_um & 0xFF);
  report[2] = (uint8_t)((pos_um >> 8) & 0xFF);
  report[3] = (uint8_t)((pos_um >> 16) & 0xFF);
  report[4] = (uint8_t)((pos_um >> 24) & 0xFF);
  report[5] = status;
  report[6] = s_error;

  return USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, report, (uint16_t)sizeof(report));
}

static void Stepper_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  TIM_OC_InitTypeDef oc = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  /* PA6 = TIM3_CH1 = PUL */
  gpio.Pin = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* PA0 = DIR */
  gpio.Pin = GPIO_PIN_0;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;                    /* 72 MHz / 72 = 1 MHz */
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = (1000000U / STEP_PULSE_HZ) - 1U;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  (void)HAL_TIM_PWM_Init(&htim3);

  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = (htim3.Init.Period + 1U) / 2U;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  (void)HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);

  HAL_NVIC_SetPriority(TIM3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

static void Stepper_Stop(void)
{
  (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
  (void)HAL_TIM_Base_Stop_IT(&htim3);
  s_moving = 0U;
  s_steps_left = 0U;
}

/**
  * @brief  Stop and define the current position as the 0 point.
  */
static void Zero_Set(void)
{
  Stepper_Stop();
  s_pos_steps = 0;
  s_homed = 1U;
  s_homing = 0U;
  s_key_hit = 0U;
  s_error = ERR_NONE;
}

static void Stepper_Run(uint32_t steps, int8_t dir)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, (dir > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  s_dir = dir;
  s_steps_left = steps;
  s_moving = 1U;
  s_key_hit = 0U;
  __HAL_TIM_SET_COUNTER(&htim3, 0U);
  (void)HAL_TIM_Base_Start_IT(&htim3);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

/**
  * @brief  Move toward the reset switch (negative), up to 300 mm.
  */
static void Home_Start(void)
{
  Stepper_Stop();
  s_error = ERR_NONE;
  s_homed = 0U;
  s_homing = 1U;
  Stepper_Run(MAX_TRAVEL_STEPS, -1);
}

/**
  * @brief  Manual jog before homing, max +/-5 mm per command.
  */
static void Stepper_Jog(int32_t delta_um)
{
  int64_t steps;
  int8_t  dir;

  Stepper_Stop();
  s_homing = 0U;
  s_error = ERR_NONE;

  if (s_homed != 0U)
  {
    s_error = ERR_LIMIT;      /* only used before homing */
    return;
  }

  if (delta_um == 0)
  {
    return;
  }

  if ((delta_um > JOG_MAX_UM) || (delta_um < -JOG_MAX_UM))
  {
    s_error = ERR_LIMIT;
    return;
  }

  dir = (delta_um > 0) ? 1 : -1;
  steps = ((int64_t)((delta_um > 0) ? delta_um : -delta_um) * STEPS_PER_REV
           + (MM_PER_REV * 1000 / 2)) / (MM_PER_REV * 1000);
  if (steps <= 0)
  {
    return;
  }

  Stepper_Run((uint32_t)steps, dir);
}

static void Stepper_MoveTo(int32_t target_um)
{
  int32_t pos_um;
  int32_t delta_um;
  int64_t delta_steps;
  int32_t target_steps;
  int8_t  dir;

  Stepper_Stop();
  s_homing = 0U;
  s_error = ERR_NONE;

  if (s_homed == 0U)
  {
    s_error = ERR_NOT_HOMED;
    return;
  }

  if ((target_um < 0) || (target_um > MAX_TRAVEL_UM))
  {
    s_error = ERR_LIMIT;
    return;
  }

  pos_um = (int32_t)(((int64_t)s_pos_steps * (MM_PER_REV * 1000)) / STEPS_PER_REV);
  delta_um = target_um - pos_um;
  if (delta_um == 0)
  {
    return;
  }

  dir = (delta_um > 0) ? 1 : -1;

  /* steps = |delta_um| * 1600 / 60000, rounded */
  delta_steps = ((int64_t)((delta_um > 0) ? delta_um : -delta_um) * STEPS_PER_REV
                 + (MM_PER_REV * 1000 / 2)) / (MM_PER_REV * 1000);
  if (delta_steps <= 0)
  {
    return;
  }

  target_steps = s_pos_steps + (int32_t)(dir * delta_steps);
  if ((target_steps < 0) || (target_steps > MAX_TRAVEL_STEPS))
  {
    s_error = ERR_LIMIT;
    return;
  }

  Stepper_Run((uint32_t)delta_steps, dir);
}

/**
  * @brief  20 ms debounce on PB12 (active low) and report changes over USB HID.
  */
static void Key_Process(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t  raw = (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

  if (raw != key_last_raw)
  {
    key_last_raw = raw;
    key_change_ms = now;
  }

  if (((now - key_change_ms) >= 20U) && (key_stable != raw))
  {
    key_stable = raw;
    key_dirty = 1U;

    if (key_stable != 0U)
    {
      Zero_Set();
    }
  }

  if ((key_dirty != 0U) && (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED))
  {
    if (Key_SendReport(key_stable) == USBD_OK)
    {
      key_dirty = 0U;
    }
  }
}

void TIM3_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim3);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if ((htim->Instance == TIM3) && (s_moving != 0U))
  {
    /* Reset switch only stops motion toward the switch (negative). */
    if ((s_dir < 0) && (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET))
    {
      (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
      (void)HAL_TIM_Base_Stop_IT(&htim3);
      s_moving = 0U;
      s_steps_left = 0U;
      s_key_hit = 1U;
      return;
    }

    s_pos_steps += s_dir;

    if (s_steps_left > 0U)
    {
      s_steps_left--;
    }

    if (s_steps_left == 0U)
    {
      (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
      (void)HAL_TIM_Base_Stop_IT(&htim3);
      s_moving = 0U;

      if (s_homing != 0U)
      {
        s_homing = 0U;
        s_error = ERR_HOME_FAIL;
      }
    }
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  Stepper_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Key_Process();

    if (s_key_hit != 0U)
    {
      s_key_hit = 0U;
      Zero_Set();
    }

    if ((HAL_GetTick() - telemetry_ms) >= 20U)
    {
      telemetry_ms = HAL_GetTick();
      (void)Telemetry_Send();
    }

    if (g_hid_reset_req != 0U)
    {
      g_hid_reset_req = 0U;
      Home_Start();
    }

    if (g_hid_jog_req != 0U)
    {
      g_hid_jog_req = 0U;
      Stepper_Jog(g_hid_jog_um);
    }

    if (g_hid_move_req != 0U)
    {
      g_hid_move_req = 0U;
      Stepper_MoveTo(g_hid_target_um);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
