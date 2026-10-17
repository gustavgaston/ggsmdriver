#include "board_time.h"

#include "main.h"

static TIM_HandleTypeDef htim17;
static volatile uint16_t micros_high = 0;

void board_time_init(void) {
  htim17.Instance = TIM17;
  htim17.Init.Prescaler = (BOARD_TIMER_CLOCK_HZ / (BOARD_TIMER_TICKS_PER_US * 1000000UL)) - 1UL;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 0xFFFFU;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
    Error_Handler();
  /* HAL_TIM_Base_Init generates an update event to load the prescaler. */
  TIM17->SR = 0;
  TIM17->CNT = 0;
  micros_high = 0;
  __HAL_TIM_ENABLE_IT(&htim17, TIM_IT_UPDATE);
  __HAL_TIM_ENABLE(&htim17);
}

void board_time_irq(void) {
  if (TIM17->SR & TIM_SR_UIF) {
    TIM17->SR = (uint32_t)~TIM_SR_UIF;
    ++micros_high;
  }
}

uint32_t micros(void) {
  uint32_t primask = __get_PRIMASK();
  uint16_t high;
  uint16_t count;
  __disable_irq();
  high = micros_high;
  count = (uint16_t)TIM17->CNT;
  /* The counter wrapped but the interrupt has not counted it yet. A count
     in the upper half means the wrap happened after CNT was read. */
  if ((TIM17->SR & TIM_SR_UIF) && count < 0x8000U)
    ++high;
  __set_PRIMASK(primask);
  return ((uint32_t)high << 16) | count;
}

void delay_us(uint32_t us) {
  /* Waiting for us + 1 counter increments from an unknown phase inside the
     current tick lasts at least us microseconds. */
  uint16_t start = (uint16_t)TIM17->CNT;
  while ((uint16_t)((uint16_t)TIM17->CNT - start) < (uint16_t)(us + 1U)) {
  }
}

void delay_ms(uint32_t ms) {
  uint32_t started = micros();
  uint32_t wait = ms * 1000UL;
  /* Chunks of 1 s keep ms * 1000 and the comparison far below 2^32 us. */
  while (wait) {
    uint32_t chunk = wait > 1000000UL ? 1000000UL : wait;
    while ((uint32_t)(micros() - started) < chunk) {
    }
    started += chunk;
    wait -= chunk;
  }
}
