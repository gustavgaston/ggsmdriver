#include "ggstepper_hw.h"

#include "board_time.h"
#include "main.h"
#include "stm32g0xx_hal.h"

#define GGSTEPPER_TIMER_PRESCALER \
  ((BOARD_TIMER_CLOCK_HZ / (GGSTEPPER_TIMER_TICKS_PER_US * 1000000UL)) - 1UL)

#if BOARD_TIMER_TICKS_PER_US != GGSTEPPER_TIMER_TICKS_PER_US
#error "STEP timer and micros() timer tick rates must match"
#endif
#if (BOARD_TIMER_CLOCK_HZ % (GGSTEPPER_TIMER_TICKS_PER_US * 1000000UL)) != 0 || \
    GGSTEPPER_TIMER_PRESCALER > 65535UL
#error "STEP timer tick rate cannot be derived from the timer clock"
#endif

#define UART_TIMEOUT_US (GGSTEPPER_UART_READ_TIMEOUT_MS * 1000UL)

static TIM_HandleTypeDef htim3;
static UART_HandleTypeDef huart2;

void ggstepper_hw_init(void) {
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Driver off (ENN high) and STEP low before the pins become outputs. */
  HAL_GPIO_WritePin(TMC_EN_PORT, TMC_EN_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TMC_STEP_PORT, TMC_STEP_PIN, GPIO_PIN_RESET);

  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin = TMC_EN_PIN;
  HAL_GPIO_Init(TMC_EN_PORT, &gpio);
  gpio.Pin = TMC_STEP_PIN;
  HAL_GPIO_Init(TMC_STEP_PORT, &gpio);
  gpio.Pin = TMC_DIR_PIN;
  HAL_GPIO_Init(TMC_DIR_PORT, &gpio);

  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Pin = TMC_DIAG_PIN;
  HAL_GPIO_Init(TMC_DIAG_PORT, &gpio);

  /* STEP timer: counts 1 us ticks, free-running up to the top value. */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = GGSTEPPER_TIMER_PRESCALER;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 0xFFFFU;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
    Error_Handler();
  TIM3->CR1 &= ~TIM_CR1_CEN;
  TIM3->DIER = 0;
  TIM3->SR = 0;

  huart2.Instance = TMC_UART_INSTANCE;
  huart2.Init.BaudRate = GGSTEPPER_UART_BAUD;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
    Error_Handler();
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
    Error_Handler();
}

void ggstepper_hw_enable_driver(bool enable) {
  HAL_GPIO_WritePin(TMC_EN_PORT, TMC_EN_PIN, enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void ggstepper_hw_diag_interrupt_enable(void) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = TMC_DIAG_PIN;
  gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(TMC_DIAG_PORT, &gpio);
  __HAL_GPIO_EXTI_CLEAR_RISING_IT(TMC_DIAG_PIN);
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(TMC_DIAG_PIN);
  HAL_NVIC_SetPriority(TMC_DIAG_IRQn, GGSTEPPER_DIAG_IRQ_PRIORITY, 0);
  HAL_NVIC_EnableIRQ(TMC_DIAG_IRQn);
}

/* Called from EXTI0_1_IRQHandler. */
void ggstepper_hw_exti_irq(void) {
  if (__HAL_GPIO_EXTI_GET_RISING_IT(TMC_DIAG_PIN) ||
      __HAL_GPIO_EXTI_GET_FALLING_IT(TMC_DIAG_PIN)) {
    __HAL_GPIO_EXTI_CLEAR_RISING_IT(TMC_DIAG_PIN);
    __HAL_GPIO_EXTI_CLEAR_FALLING_IT(TMC_DIAG_PIN);
    ggstepper_diag_isr();
  }
}

/* ---- STEP timer ---- */

void ggstepper_hw_timer_prepare(void) {
  TIM3->CR1 &= ~TIM_CR1_CEN;
  TIM3->ARR = 0xFFFFU;
  TIM3->CNT = 0;
  TIM3->SR = 0;
  NVIC_ClearPendingIRQ(TIM3_TIM4_IRQn);
  TIM3->DIER = TIM_DIER_UIE;
}

void ggstepper_hw_timer_start(void) {
  TIM3->CR1 |= TIM_CR1_CEN;
}

void ggstepper_hw_timer_stop(void) {
  TIM3->CR1 &= ~TIM_CR1_CEN;
  TIM3->DIER = 0;
  TIM3->SR = 0;
  NVIC_ClearPendingIRQ(TIM3_TIM4_IRQn);
}

uint32_t ggstepper_hw_timer_count(void) {
  return TIM3->CNT;
}

void ggstepper_hw_timer_set_top(uint16_t top) {
  TIM3->ARR = top;
}

/* Called from TIM3_TIM4_IRQHandler. The flag check also drops an interrupt
   that was already pending when the timer was stopped. */
void ggstepper_hw_timer_irq(void) {
  if (TIM3->SR & TIM_SR_UIF) {
    TIM3->SR = (uint32_t)~TIM_SR_UIF;
    ggstepper_timer_isr();
  }
}

/* ---- TMC2209 UART ---- */

bool ggstepper_hw_uart_rx_high(void) {
  return (TMC_UART_PORT->IDR & TMC_UART_RX_PIN) != 0U;
}

void ggstepper_hw_uart_discard_input(void) {
  while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE))
    (void)huart2.Instance->RDR;
  __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
}

enum ggstepper_hw_uart_result ggstepper_hw_uart_receive(uint8_t *value, uint32_t started_us,
                                                        uint32_t timeout_us) {
  for (;;) {
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE)) {
      bool damaged = __HAL_UART_GET_FLAG(&huart2, UART_FLAG_FE) ||
                     __HAL_UART_GET_FLAG(&huart2, UART_FLAG_NE) ||
                     __HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE);
      *value = (uint8_t)huart2.Instance->RDR;
      if (damaged) {
        __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
        return GGSTEPPER_HW_UART_FRAME;
      }
      return GGSTEPPER_HW_UART_OK;
    }
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE) ||
        __HAL_UART_GET_FLAG(&huart2, UART_FLAG_FE) ||
        __HAL_UART_GET_FLAG(&huart2, UART_FLAG_NE)) {
      __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
      return GGSTEPPER_HW_UART_FRAME;
    }
    if ((uint32_t)(micros() - started_us) >= timeout_us)
      return GGSTEPPER_HW_UART_TIMEOUT;
  }
}

enum ggstepper_hw_uart_result ggstepper_hw_uart_send(const uint8_t *bytes, uint8_t count) {
  enum ggstepper_hw_uart_result first_failure = GGSTEPPER_HW_UART_OK;
  bool wait_echo = true;
  uint8_t index;
  uint32_t started;

  ggstepper_hw_uart_discard_input();
  for (index = 0; index < count; ++index) {
    started = micros();
    while (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TXE)) {
      if ((uint32_t)(micros() - started) >= UART_TIMEOUT_US)
        return first_failure == GGSTEPPER_HW_UART_OK ? GGSTEPPER_HW_UART_TIMEOUT : first_failure;
    }
    huart2.Instance->TDR = bytes[index];
    if (wait_echo) {
      uint8_t echo = 0;
      enum ggstepper_hw_uart_result result =
          ggstepper_hw_uart_receive(&echo, micros(), UART_TIMEOUT_US);
      if (result == GGSTEPPER_HW_UART_OK && echo != bytes[index])
        result = GGSTEPPER_HW_UART_FRAME;
      if (result != GGSTEPPER_HW_UART_OK) {
        first_failure = result;
        wait_echo = false;
      }
    }
  }
  /* Without echo to wait for, let the last byte leave the shift register. */
  started = micros();
  while (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC)) {
    if ((uint32_t)(micros() - started) >= UART_TIMEOUT_US)
      break;
  }
  return first_failure;
}
