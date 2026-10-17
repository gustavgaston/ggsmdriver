/* STM32G0 hardware layer for ggstepper: the pieces the AVR build did with
   Timer1, direct port writes, a bit-banged UART and a pin-change interrupt.

   Resources owned by ggstepper:
     TIM3     STEP timer, 1 MHz tick, update interrupt = the old Timer1 compare ISR
     PA6      STEP (driven as a plain output; it is also TIM3_CH1 but PWM is not used)
     PA7      DIR
     PB0      ENN (active low)
     PB1      DIAG on EXTI1, both edges (the old pin-change ISR)
     USART2   TMC2209 UART (PA2 TX through 1 k, PA3 RX wired to the same line)
   Time comes from board_time.h (TIM17). */
#ifndef GGSTEPPER_HW_H
#define GGSTEPPER_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"
#include "ggstepper_conf.h"

/* ---- interrupt priorities (0 = highest; the Cortex-M0+ has four levels) ----
   The STEP timer and DIAG preempt everything else. The HAL tick and the TIM17
   microsecond clock come next, because the USB library calls HAL_Delay from
   the USB interrupt (priority 2). */
#define GGSTEPPER_STEP_TIMER_IRQ_PRIORITY 0U
#define GGSTEPPER_DIAG_IRQ_PRIORITY 0U

/* ---- fast pin access (also used from interrupts) ---- */
#define GGSTEPPER_STEP_HIGH() (TMC_STEP_PORT->BSRR = TMC_STEP_PIN)
#define GGSTEPPER_STEP_LOW() (TMC_STEP_PORT->BRR = TMC_STEP_PIN)
#define GGSTEPPER_DIR_WRITE(high) \
  (TMC_DIR_PORT->BSRR = (high) ? TMC_DIR_PIN : ((uint32_t)TMC_DIR_PIN << 16))
#define GGSTEPPER_DIAG_READ() ((TMC_DIAG_PORT->IDR & TMC_DIAG_PIN) != 0U)

/* ---- interrupt masking (replaces SREG save + cli) ---- */
static inline uint32_t ggstepper_irq_save(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}
static inline void ggstepper_irq_restore(uint32_t primask) {
  __set_PRIMASK(primask);
}

/* ---- setup ---- */
/* Configures the pins (ENN high = driver off, STEP low), USART2 and TIM3.
   DIAG is configured as an input but its interrupt stays off. */
void ggstepper_hw_init(void);
void ggstepper_hw_diag_interrupt_enable(void);
void ggstepper_hw_enable_driver(bool enable);

/* ---- STEP timer (Timer1 CTC replacement) ----
   The counter runs 0..top and an update interrupt fires when it wraps, so a
   period is top + 1 ticks, exactly like AVR CTC with OCR1A = top. The new top
   takes effect immediately (auto-reload is not buffered). */
void ggstepper_hw_timer_prepare(void); /* stopped, count 0, top 65535, interrupt armed */
void ggstepper_hw_timer_start(void);
void ggstepper_hw_timer_stop(void);    /* stops, disarms and clears a pending interrupt */
uint32_t ggstepper_hw_timer_count(void);
void ggstepper_hw_timer_set_top(uint16_t top);

/* Interrupt handler bodies, called from the vectors in stm32g0xx_it.c. */
void ggstepper_hw_timer_irq(void);
void ggstepper_hw_exti_irq(void);

/* Implemented by ggstepper.c; called from the handlers above. */
void ggstepper_timer_isr(void);
void ggstepper_diag_isr(void);

/* ---- TMC2209 UART ----
   The RX pin sees everything the TX pin sends. Every transmitted byte is
   therefore read back and discarded (and compared) before the next byte goes
   out, so the echo never reaches the reply parser. */
enum ggstepper_hw_uart_result {
  GGSTEPPER_HW_UART_OK,
  GGSTEPPER_HW_UART_TIMEOUT, /* nothing arrived in time (for send: no echo) */
  GGSTEPPER_HW_UART_FRAME    /* framing/noise/overrun error (for send: echo differs) */
};
/* Sends count bytes. After a failed echo the remaining bytes are still sent
   (without waiting for their echo) so the driver is never left with half a
   frame; the first failure is returned. */
enum ggstepper_hw_uart_result ggstepper_hw_uart_send(const uint8_t *bytes, uint8_t count);
/* Waits for one received byte; started_us is a micros() value, timeout_us the
   total time allowed since then. */
enum ggstepper_hw_uart_result ggstepper_hw_uart_receive(uint8_t *value, uint32_t started_us,
                                                        uint32_t timeout_us);
bool ggstepper_hw_uart_rx_high(void);
void ggstepper_hw_uart_discard_input(void);

#endif
