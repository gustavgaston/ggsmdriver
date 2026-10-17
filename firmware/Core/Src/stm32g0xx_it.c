/* Interrupt handlers. Priorities (0 = highest):
     0  TIM3 (STEP timer), EXTI1 (DIAG)       ggstepper_hw.h
     1  SysTick (HAL tick), TIM17 (micros())  stm32g0xx_hal_conf.h, msp
     2  USB                                   usbd_conf.c
   The USB library calls HAL_Delay from the USB interrupt, so SysTick must
   preempt USB. */
#include "stm32g0xx_it.h"

#include "board_time.h"
#include "ggstepper_hw.h"
#include "main.h"

extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

/******************************************************************************/
/*           Cortex-M0+ Processor Interruption and Exception Handlers          */
/******************************************************************************/
void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  /* STEP is a plain output: park it low so a fault never leaves it high. */
  GGSTEPPER_STEP_LOW();
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* STM32G0xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/
void EXTI0_1_IRQHandler(void)
{
  ggstepper_hw_exti_irq();
}

void TIM3_TIM4_IRQHandler(void)
{
  ggstepper_hw_timer_irq();
}

void TIM17_FDCAN_IT1_IRQHandler(void)
{
  board_time_irq();
}

void USB_UCPD1_2_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_DRD_FS);
}
