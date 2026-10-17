/* GGSMDriver pin map. Source: /requirements.md (pin map table), verified
   against the schematic generator (hardware/ggsmdriver.py) for the LED.
   Pins not listed here (PA0/PA1 encoder, PA4/PA5 IO1/IO2, PA11/PA12 USB,
   PA13/PA14 SWD) are not configured by this firmware. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32g0xx_hal.h"

/* TMC2209 STEP. The pin is TIM3_CH1, but the firmware drives it as a plain
   output: TIM3 only times the compare interrupt (see ggstepper_hw.c). */
#define TMC_STEP_PORT GPIOA
#define TMC_STEP_PIN GPIO_PIN_6
#define TMC_DIR_PORT GPIOA
#define TMC_DIR_PIN GPIO_PIN_7
/* ENN, active low, external pull-up: the driver is off until the MCU enables it. */
#define TMC_EN_PORT GPIOB
#define TMC_EN_PIN GPIO_PIN_0
/* DIAG on EXTI line 1. */
#define TMC_DIAG_PORT GPIOB
#define TMC_DIAG_PIN GPIO_PIN_1
#define TMC_DIAG_IRQn EXTI0_1_IRQn
/* Status LED: PB2 -> 1 k -> LED -> GND, so high = on. */
#define LED_STATUS_PORT GPIOB
#define LED_STATUS_PIN GPIO_PIN_2

/* TMC2209 UART on USART2: PA2 TX (through 1 k to PDN_UART), PA3 RX (direct
   to PDN_UART, so every transmitted byte is echoed on RX). */
#define TMC_UART_INSTANCE USART2
#define TMC_UART_PORT GPIOA
#define TMC_UART_TX_PIN GPIO_PIN_2
#define TMC_UART_RX_PIN GPIO_PIN_3
#define TMC_UART_AF GPIO_AF1_USART2

#endif
