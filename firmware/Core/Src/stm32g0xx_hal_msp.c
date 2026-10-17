/* HAL MSP: peripheral clocks, pins and interrupt priorities for the HAL
   peripherals this firmware uses (TIM3, TIM17, USART2). The USB peripheral's
   MSP is in USB_Device/Target/usbd_conf.c. */
#include "board_pins.h"
#include "ggstepper_hw.h"
#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    /* STEP timer; its interrupt is only unmasked inside the timer (DIER). */
    __HAL_RCC_TIM3_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM3_TIM4_IRQn, GGSTEPPER_STEP_TIMER_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(TIM3_TIM4_IRQn);
  }
  else if (htim->Instance == TIM17)
  {
    /* microsecond clock (board_time.c) */
    __HAL_RCC_TIM17_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM17_FDCAN_IT1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM17_FDCAN_IT1_IRQn);
  }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef gpio = {0};
  if (huart->Instance == TMC_UART_INSTANCE)
  {
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /* TX drives PDN_UART through the board's 1 k resistor, push-pull. RX is
       wired straight to PDN_UART; the pull-up (as INPUT_PULLUP on the AVR)
       keeps the line idle high when the TMC2209 does not drive it.
       TODO(verify): pull-up kept from the AVR build; the TMC2209's own
       PDN_UART pull-up situation was not checked in the datasheet. */
    gpio.Pin = TMC_UART_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = TMC_UART_AF;
    HAL_GPIO_Init(TMC_UART_PORT, &gpio);
    gpio.Pin = TMC_UART_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(TMC_UART_PORT, &gpio);
  }
}
