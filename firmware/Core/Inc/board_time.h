/* Free-running microsecond clock and busy-wait delays.

   micros(): TIM17 is a 16-bit up-counter ticking at 1 MHz. Its update
   interrupt extends it to 32 bits in software, so micros() wraps every
   2^32 us (about 71.6 minutes), like Arduino's micros(). Differences of
   uint32_t values are valid up to that span.

   TIM17 was chosen because TIM2 (the only 32-bit timer) is reserved for the
   encoder on PA0/PA1, TIM3 is the STEP timer, and the Cortex-M0+ has no DWT
   cycle counter. SysTick is left to the HAL tick.

   micros() may be called from any context, including interrupts and with
   interrupts disabled for up to about 32 ms. */
#ifndef BOARD_TIME_H
#define BOARD_TIME_H

#include <stdint.h>

/* Timer kernel clock. SystemClock_Config() makes SYSCLK = HCLK = PCLK = 64 MHz
   and main() refuses to start if SystemCoreClock differs. */
#define BOARD_TIMER_CLOCK_HZ 64000000UL
#define BOARD_TIMER_TICKS_PER_US 1UL

void board_time_init(void);
uint32_t micros(void);
/* Blocks for at least us microseconds (at most us + 1). us <= 60000. */
void delay_us(uint32_t us);
/* Blocks for at least ms milliseconds, measured with micros(). */
void delay_ms(uint32_t ms);
/* TIM17 update interrupt body; called from the vector in stm32g0xx_it.c. */
void board_time_irq(void);

#endif
