/* GGSMDriver firmware: ggstepper core (TMC2209 setup, StallGuard homing,
   normalized moves) plus the Automato serial bridge over USB CDC.

   Same job as the ggstepper part of devices/nodharmonica/nodharmonica_automato:
   init, home, register the ggstepper position and speed controls, run the
   bridge, poll. The fan and the other devices of that sketch are not here. */
#include "main.h"

#include "automato_bridge.h"
#include "board_time.h"
#include "ggstepper_automato.h"
#include "ggstepper_hw.h"
#include "serial.h"
#include "usb_device.h"

/* ---- Adjustable constants ---- */
/* Device name in the Automato descriptor. The Automato host compares it with
   the device chosen in its registry (src/automato/registry.py). */
#define AUTOMATO_DEVICE_NAME "ggsmdriver" /* TODO(verify): the name the Automato registry expects for this board */
/* Acceleration for every move (steps/s^2): ggstepper_automato.h defaults to
   GGSTEPPER_PROFILE_MAX_ACCELERATION, as in the Nodharmonica sketch (where the
   override is commented out). */
/* #define GGSTEPPER_AUTOMATO_ACCELERATION 240000UL */

static const struct automato_control CONTROLS[] = {
  GGSTEPPER_AUTOMATO_POSITION("ggstepper1.position"),
  GGSTEPPER_AUTOMATO_SPEED("ggstepper1.speed"),
};

static void SystemClock_Config(void);
static void LED_Init(void);

int main(void)
{
  bool homed;

  HAL_Init();
  SystemClock_Config();
  if (SystemCoreClock != BOARD_TIMER_CLOCK_HZ)
  {
    Error_Handler();
  }
  board_time_init();
  LED_Init();

  serial_init();
  MX_USB_Device_Init();

  ggstepper_init();
  homed = ggstepper_home();
  /* TODO(verify): LED behaviour is this port's choice (the AVR sketch has none):
     on = homed, off = homing failed or not done. */
  HAL_GPIO_WritePin(LED_STATUS_PORT, LED_STATUS_PIN, homed ? GPIO_PIN_SET : GPIO_PIN_RESET);

  automato_begin(AUTOMATO_DEVICE_NAME, CONTROLS, sizeof(CONTROLS) / sizeof(CONTROLS[0]));
  (void)serial_take_open_event(); /* the descriptor was just printed */

  for (;;)
  {
    ggstepper_poll();
    serial_poll();
    if (serial_take_open_event())
    {
      automato_announce();
    }
    automato_poll();
  }
}

/**
  * @brief System clock: 64 MHz from HSI16 through the PLL (no crystal), USB
  *        clock from HSI48 (enabled in HAL_PCD_MspInit) trimmed by the CRS
  *        against the USB start-of-frame. As in ST's CDC_Standalone example.
  */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  /* Voltage scale 1 for 64 MHz. */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* CRS: trim HSI48 from the USB SOF (1 kHz). */
  __HAL_RCC_CRS_CLK_ENABLE();
  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;
  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);
}

static void LED_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(LED_STATUS_PORT, LED_STATUS_PIN, GPIO_PIN_RESET);
  gpio.Pin = LED_STATUS_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_STATUS_PORT, &gpio);
}

/**
  * @brief  Unrecoverable error: stop interrupts, leave STEP low and the
  *         driver disabled (ENN high), light the status LED and stop.
  */
void Error_Handler(void)
{
  __disable_irq();
  /* Direct register writes: the GPIO clocks may not be on yet, which is fine
     because the pins keep their reset state (inputs, ENN pulled up). */
  if ((RCC->IOPENR & (RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN)) ==
      (RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN))
  {
    GGSTEPPER_STEP_LOW();
    TMC_EN_PORT->BSRR = TMC_EN_PIN;
    LED_STATUS_PORT->BSRR = LED_STATUS_PIN;
  }
  while (1)
  {
  }
}
