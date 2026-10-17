#include "ggstepper.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "board_time.h"
#include "ggstepper_hw.h"
#if GGSTEPPER_DEBUG
#include "serial.h"
#endif

#define GGSTEPPER_DIAG_IGNORE_STEPS \
  ((uint32_t)GGSTEPPER_FULLSTEPS * GGSTEPPER_MICROSTEPS * GGSTEPPER_RPS * \
   GGSTEPPER_DIAG_IGNORE_MS / 1000UL)
/* A variable, so a configured value of 0 does not trip -Wtype-limits. */
static const uint32_t ggstepper_diag_ignore_steps = GGSTEPPER_DIAG_IGNORE_STEPS;
#define GGSTEPPER_IHOLD_IRUN_VALUE \
  (((uint32_t)GGSTEPPER_IHOLDDELAY << 16) | \
   ((uint32_t)GGSTEPPER_IRUN << 8) | GGSTEPPER_IHOLD)

/* TIM3 is owned by ggstepper while an asynchronous move is active. Its tick
   (GGSTEPPER_TIMER_TICKS_PER_US, from ggstepper_conf.h) defines the planner's
   time base. */
#define GGSTEPPER_TIMER_TICKS_PER_SECOND \
  (GGSTEPPER_TIMER_TICKS_PER_US * 1000000UL)
#define GGSTEPPER_STEP_HIGH_TICKS \
  (GGSTEPPER_STEP_HIGH_US * GGSTEPPER_TIMER_TICKS_PER_US)
/* Shortest step period the executor accepts: the pulse, the deadline margin
   the scheduler needs and the planner's two spare ticks. This is what the
   timer can represent, not a measured interrupt-time limit: a period that the
   interrupt cannot meet stops the move and invalidates the position at run
   time. (The AVR build used 128 ticks of 0.5 us = 64 us.)
   TODO(verify): measure the worst-case TIM3 interrupt time (pulse + planner)
   on hardware; it, not this value, bounds the usable step rate. */
#define GGSTEPPER_ASYNC_MIN_PERIOD_TICKS \
  (GGSTEPPER_STEP_HIGH_TICKS + GGSTEPPER_DEADLINE_MARGIN_TICKS + 2UL)

#if GGSTEPPER_TIMER_TICKS_PER_US < 1
#error "ggstepper timer clock is too slow"
#endif
#if GGSTEPPER_STEP_HIGH_TICKS < 1 || GGSTEPPER_STEP_HIGH_TICKS > 65536UL
#error "GGSTEPPER_STEP_HIGH_US is outside the STEP timer range"
#endif
#if GGSTEPPER_PROFILE_MAX_VELOCITY > \
    (GGSTEPPER_TIMER_TICKS_PER_SECOND / GGSTEPPER_ASYNC_MIN_PERIOD_TICKS) || \
    GGSTEPPER_ASYNC_MIN_PERIOD_TICKS <= GGSTEPPER_STEP_HIGH_TICKS + 2UL
#error "GGSTEPPER_PROFILE_MAX_VELOCITY is too fast for the configured pulse width"
#endif
#if GGSTEPPER_STEPS_PER_REV == 0 || GGSTEPPER_STEPS_PER_REV > 26843545UL
#error "steps/revolution and derived provisional limits must fit uint32_t"
#endif
#if GGSTEPPER_PROFILE_MIN_VELOCITY == 0 || \
    GGSTEPPER_PROFILE_MIN_VELOCITY > GGSTEPPER_PROFILE_MAX_VELOCITY || \
    GGSTEPPER_PROFILE_DEFAULT_VELOCITY < GGSTEPPER_PROFILE_MIN_VELOCITY || \
    GGSTEPPER_PROFILE_DEFAULT_VELOCITY > GGSTEPPER_PROFILE_MAX_VELOCITY
#error "ggstepper velocity profile limits/default are inconsistent"
#endif
#if GGSTEPPER_PROFILE_MIN_ACCELERATION == 0 || \
    GGSTEPPER_PROFILE_MIN_ACCELERATION > GGSTEPPER_PROFILE_MAX_ACCELERATION || \
    GGSTEPPER_PROFILE_MAX_ACCELERATION > 2147483647UL || \
    GGSTEPPER_PROFILE_DEFAULT_ACCELERATION < GGSTEPPER_PROFILE_MIN_ACCELERATION || \
    GGSTEPPER_PROFILE_DEFAULT_ACCELERATION > GGSTEPPER_PROFILE_MAX_ACCELERATION
#error "ggstepper acceleration profile limits/default are inconsistent"
#endif
#if GGSTEPPER_HOMING_VELOCITY == 0 || \
    GGSTEPPER_HOMING_VELOCITY > \
        (GGSTEPPER_TIMER_TICKS_PER_SECOND / GGSTEPPER_ASYNC_MIN_PERIOD_TICKS) || \
    GGSTEPPER_HOMING_ACCELERATION == 0 || \
    GGSTEPPER_HOMING_MAX_SEEK_STEPS == 0 || \
    GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS == 0 || \
    GGSTEPPER_HOMING_MAX_SEEK_STEPS > \
        (2147483647UL / GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS) || \
    GGSTEPPER_CONTACT_CONFIRMATIONS == 0 || \
    GGSTEPPER_CONTACT_CONFIRMATIONS > GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS
#error "ggstepper homing profile/bounds are invalid"
#endif

static volatile bool ggstepper_endpoint_hit = false;
static volatile bool ggstepper_diag_armed = false;

#if GGSTEPPER_DEBUG
static volatile bool ggstepper_diag_was_high = false;
static volatile uint8_t ggstepper_debug_pending = 0;
static volatile uint8_t ggstepper_debug_armed = 0;
static volatile uint8_t ggstepper_debug_forward = 0;
static volatile uint8_t ggstepper_debug_sources = 0;
static volatile bool ggstepper_current_forward = true;
static uint32_t ggstepper_debug_sequence = 0;

enum ggstepper_read_result {
  GGSTEPPER_READ_OK,
  GGSTEPPER_READ_TIMEOUT,
  GGSTEPPER_READ_FRAME,
  GGSTEPPER_READ_CRC
};
#endif

volatile int32_t ggstepper_position = 0;
int32_t ggstepper_min_position = 0;
int32_t ggstepper_max_position = 0;
uint32_t ggstepper_travel_steps = 0;

static volatile bool ggstepper_homed = false;
static volatile bool ggstepper_moving = false;
static volatile bool ggstepper_async_step_high = false;
static volatile bool ggstepper_async_stop_requested = false;
static volatile bool ggstepper_async_brake_requested = false;
static volatile bool ggstepper_async_forward = true;
static volatile uint32_t ggstepper_async_steps_remaining = 0;
static volatile uint32_t ggstepper_async_wait_ticks = 0;
static volatile bool ggstepper_async_timer_fault = false;
static volatile int32_t ggstepper_async_target = 0;
static volatile uint32_t ggstepper_motion_speed_squared = 0;
static volatile uint32_t ggstepper_motion_cruise_velocity = 0;
static volatile uint32_t ggstepper_motion_acceleration = 0;
/* Per-move constants and the current velocity, so the per-pulse planner uses
   only 32-bit arithmetic and skips square root/division while cruising. */
static volatile uint32_t ggstepper_motion_velocity = 0;
static volatile uint32_t ggstepper_motion_cruise_squared = 0;
static volatile uint32_t ggstepper_motion_cruise_ticks = 0;
static volatile uint32_t ggstepper_motion_two_acceleration = 0;
static volatile uint32_t ggstepper_motion_stopping_saturation = 0;

/* True once the current move has emitted a pulse and has not finished; only
   then can an abort lose position. */
static volatile bool ggstepper_async_move_started = false;


static uint32_t ggstepper_profile_velocity =
    GGSTEPPER_PROFILE_DEFAULT_VELOCITY;
static uint32_t ggstepper_profile_acceleration =
    GGSTEPPER_PROFILE_DEFAULT_ACCELERATION;

static uint8_t ggstepper_tmc_crc(const uint8_t *bytes, uint8_t count) {
  uint8_t crc = 0;
  while (count--) {
    uint8_t current = *bytes++;
    uint8_t bit;
    for (bit = 0; bit < 8; ++bit) {
      crc = ((crc >> 7) ^ (current & 1))
                ? (uint8_t)((crc << 1) ^ 0x07)
                : (uint8_t)(crc << 1);
      current >>= 1;
    }
  }
  return crc;
}

static uint8_t ggstepper_microstep_mres(void) {
  uint16_t setting = 256;
  uint8_t mres = 0;
  while (setting > GGSTEPPER_MICROSTEPS) {
    setting >>= 1;
    ++mres;
  }
  return mres;
}

#if GGSTEPPER_DEBUG
static bool ggstepper_uart_wait_idle(uint32_t started) {
  const uint16_t idle_us = 12UL * 1000000UL / GGSTEPPER_UART_BAUD;
  uint32_t high_started = micros();

  for (;;) {
    if (ggstepper_hw_uart_rx_high()) {
      if ((uint32_t)(micros() - high_started) >= idle_us)
        return true;
    } else {
      high_started = micros();
    }
    if ((uint32_t)(micros() - started) >=
        GGSTEPPER_UART_READ_TIMEOUT_MS * 1000UL)
      return false;
  }
}

static enum ggstepper_read_result ggstepper_uart_result(
    enum ggstepper_hw_uart_result result) {
  if (result == GGSTEPPER_HW_UART_OK)
    return GGSTEPPER_READ_OK;
  return result == GGSTEPPER_HW_UART_TIMEOUT ? GGSTEPPER_READ_TIMEOUT
                                             : GGSTEPPER_READ_FRAME;
}

/* The request's own echo is consumed by ggstepper_hw_uart_send(); a missing or
   different echo (TX/RX not on the same line) is reported like any other
   failed transaction. */
static enum ggstepper_read_result ggstepper_read_register(uint8_t address,
                                                           uint32_t *value) {
  uint8_t request[4] = {0x05, GGSTEPPER_DRIVER_ADDRESS, address, 0};
  uint8_t response[8];
  uint8_t byte;
  uint32_t transaction_started = micros();
  uint32_t read_started;
  enum ggstepper_read_result result;

  request[3] = ggstepper_tmc_crc(request, 3);
  if (!ggstepper_uart_wait_idle(transaction_started))
    return GGSTEPPER_READ_TIMEOUT;
  result = ggstepper_uart_result(
      ggstepper_hw_uart_send(request, sizeof(request)));
  if (result != GGSTEPPER_READ_OK)
    return result;
  read_started = micros();
  for (byte = 0; byte < sizeof(response); ++byte) {
    result = ggstepper_uart_result(ggstepper_hw_uart_receive(
        &response[byte], read_started,
        GGSTEPPER_UART_READ_TIMEOUT_MS * 1000UL));
    if (result != GGSTEPPER_READ_OK)
      return result;
  }

  if (response[0] != 0x05 || response[1] != 0xFF ||
      response[2] != address)
    return GGSTEPPER_READ_FRAME;
  if (ggstepper_tmc_crc(response, 7) != response[7])
    return GGSTEPPER_READ_CRC;

  *value = ((uint32_t)response[3] << 24) |
           ((uint32_t)response[4] << 16) |
           ((uint32_t)response[5] << 8) | response[6];
  return GGSTEPPER_READ_OK;
}

static const char *ggstepper_read_error(
    enum ggstepper_read_result result) {
  if (result == GGSTEPPER_READ_TIMEOUT)
    return "timeout";
  if (result == GGSTEPPER_READ_FRAME)
    return "frame";
  return "crc";
}

static void ggstepper_debug_print_fault(const char *name,
                                        bool *printed) {
  if (*printed)
    serial_print(",");
  serial_print(name);
  *printed = true;
}

static void ggstepper_debug_service(void) {
  uint8_t count;
  uint8_t armed;
  uint8_t forward;
  uint8_t sources;
  uint32_t sg = 0;
  uint32_t drv = 0;
  uint32_t gstat = 0;
  enum ggstepper_read_result sg_result;
  enum ggstepper_read_result drv_result;
  enum ggstepper_read_result gstat_result;
  bool faults_printed = false;
  bool uart_printed = false;
  bool driver_fault;
  uint32_t saved_primask;

  saved_primask = ggstepper_irq_save();
  count = ggstepper_debug_pending;
  if (!count) {
    ggstepper_irq_restore(saved_primask);
    return;
  }
  ggstepper_debug_pending = 0;
  armed = ggstepper_debug_armed;
  forward = ggstepper_debug_forward;
  sources = ggstepper_debug_sources;
  ggstepper_debug_sources = 0;
  ggstepper_irq_restore(saved_primask);

  /* SG_RESULT is deliberately sampled first, as soon after STEP stops as the
     foreground can reach it. */
  sg_result = ggstepper_read_register(0x41, &sg);
  drv_result = ggstepper_read_register(0x6F, &drv);
  gstat_result = ggstepper_read_register(0x01, &gstat);

  serial_print("diag seq=");
  serial_print(++ggstepper_debug_sequence);
  serial_print(" count=");
  serial_print(count);
  serial_print(" source=");
  if (sources == 2)
    serial_print("level");
  else if (sources == 3)
    serial_print("mixed");
  else
    serial_print("irq");
  serial_print(" position=");
  serial_print(ggstepper_position);
  serial_print(" dir=");
  serial_print(forward ? "F" : "R");
  serial_print(" armed=");
  serial_print(armed);
  if (sg_result == GGSTEPPER_READ_OK) {
    serial_print(" sg=");
    serial_print(sg & 0x03FFUL);
    serial_print(" sg_limit=");
    serial_print(2UL * GGSTEPPER_SGTHRS_VALUE);
  }
  if (drv_result == GGSTEPPER_READ_OK) {
    serial_print(" drv=0x");
    serial_print_hex(drv);
  }
  if (gstat_result == GGSTEPPER_READ_OK) {
    serial_print(" gstat=0x");
    serial_print_hex(gstat);
  }

  if (drv_result == GGSTEPPER_READ_OK &&
      gstat_result == GGSTEPPER_READ_OK) {
    serial_print(" faults=");
    if (!(drv & 0xFFUL) && !(gstat & 0x07UL)) {
      serial_print("none");
    } else {
      if (gstat & (1UL << 0)) ggstepper_debug_print_fault("reset", &faults_printed);
      if (gstat & (1UL << 1)) ggstepper_debug_print_fault("drv_err", &faults_printed);
      if (gstat & (1UL << 2)) ggstepper_debug_print_fault("uv_cp", &faults_printed);
      if (drv & (1UL << 0)) ggstepper_debug_print_fault("otpw", &faults_printed);
      if (drv & (1UL << 1)) ggstepper_debug_print_fault("ot", &faults_printed);
      if (drv & (1UL << 2)) ggstepper_debug_print_fault("s2ga", &faults_printed);
      if (drv & (1UL << 3)) ggstepper_debug_print_fault("s2gb", &faults_printed);
      if (drv & (1UL << 4)) ggstepper_debug_print_fault("s2vsa", &faults_printed);
      if (drv & (1UL << 5)) ggstepper_debug_print_fault("s2vsb", &faults_printed);
      if (drv & (1UL << 6)) ggstepper_debug_print_fault("ola", &faults_printed);
      if (drv & (1UL << 7)) ggstepper_debug_print_fault("olb", &faults_printed);
    }
  } else if (drv_result == GGSTEPPER_READ_OK ||
             gstat_result == GGSTEPPER_READ_OK) {
    serial_print(" faults=partial");
    if ((drv_result == GGSTEPPER_READ_OK && (drv & 0xFFUL)) ||
        (gstat_result == GGSTEPPER_READ_OK && (gstat & 0x07UL))) {
      serial_print(" flags=");
      if (gstat_result == GGSTEPPER_READ_OK) {
        if (gstat & (1UL << 0)) ggstepper_debug_print_fault("reset", &faults_printed);
        if (gstat & (1UL << 1)) ggstepper_debug_print_fault("drv_err", &faults_printed);
        if (gstat & (1UL << 2)) ggstepper_debug_print_fault("uv_cp", &faults_printed);
      }
      if (drv_result == GGSTEPPER_READ_OK) {
        if (drv & (1UL << 0)) ggstepper_debug_print_fault("otpw", &faults_printed);
        if (drv & (1UL << 1)) ggstepper_debug_print_fault("ot", &faults_printed);
        if (drv & (1UL << 2)) ggstepper_debug_print_fault("s2ga", &faults_printed);
        if (drv & (1UL << 3)) ggstepper_debug_print_fault("s2gb", &faults_printed);
        if (drv & (1UL << 4)) ggstepper_debug_print_fault("s2vsa", &faults_printed);
        if (drv & (1UL << 5)) ggstepper_debug_print_fault("s2vsb", &faults_printed);
        if (drv & (1UL << 6)) ggstepper_debug_print_fault("ola", &faults_printed);
        if (drv & (1UL << 7)) ggstepper_debug_print_fault("olb", &faults_printed);
      }
    }
  } else {
    serial_print(" faults=unavailable");
  }

  if (sg_result != GGSTEPPER_READ_OK ||
      drv_result != GGSTEPPER_READ_OK ||
      gstat_result != GGSTEPPER_READ_OK) {
    serial_print(" uart=");
#define GGSTEPPER_PRINT_UART_ERROR(label, result) do { \
      if ((result) != GGSTEPPER_READ_OK) { \
        if (uart_printed) serial_print(","); \
        serial_print(label ":"); \
        serial_print(ggstepper_read_error(result)); \
        uart_printed = true; \
      } \
    } while (0)
    GGSTEPPER_PRINT_UART_ERROR("sg", sg_result);
    GGSTEPPER_PRINT_UART_ERROR("drv", drv_result);
    GGSTEPPER_PRINT_UART_ERROR("gstat", gstat_result);
#undef GGSTEPPER_PRINT_UART_ERROR
  }

  driver_fault =
      (drv_result == GGSTEPPER_READ_OK && (drv & 0x3EUL)) ||
      (gstat_result == GGSTEPPER_READ_OK && (gstat & 0x06UL));
  serial_print(" cause=");
  if (driver_fault) {
    serial_print("driver_fault");
  } else if (sg_result == GGSTEPPER_READ_OK &&
             (sg & 0x03FFUL) <= 2UL * GGSTEPPER_SGTHRS_VALUE) {
    serial_print("stall_candidate");
  } else if (sg_result != GGSTEPPER_READ_OK &&
             drv_result != GGSTEPPER_READ_OK &&
             gstat_result != GGSTEPPER_READ_OK) {
    serial_print("unavailable");
  } else {
    serial_print("unknown");
  }
  serial_newline();
  serial_flush();

}
#endif

static void ggstepper_write_register(uint8_t address, uint32_t value) {
  uint8_t frame[8] = {0x05,
                      GGSTEPPER_DRIVER_ADDRESS,
                      (uint8_t)(address | 0x80),
                      (uint8_t)(value >> 24),
                      (uint8_t)(value >> 16),
                      (uint8_t)(value >> 8),
                      (uint8_t)value,
                      0};
  frame[7] = ggstepper_tmc_crc(frame, 7);
  /* The TMC2209 does not acknowledge writes, as before. The echo of the
     frame is consumed inside ggstepper_hw_uart_send(); its result is not
     checked here, so a disconnected UART is only visible in debug reads. */
  (void)ggstepper_hw_uart_send(frame, sizeof(frame));
  delay_ms(2);
}

static void ggstepper_configure_driver(void) {
  uint32_t chopconf =
      (GGSTEPPER_CHOPCONF_BASE &
       ~((0x0FUL << 24) | (1UL << 17) | 0x0FUL)) |
      ((uint32_t)ggstepper_microstep_mres() << 24) |
      ((uint32_t)GGSTEPPER_VSENSE << 17) | GGSTEPPER_TOFF;
  ggstepper_write_register(0x00, GGSTEPPER_GCONF_VALUE);
  ggstepper_write_register(0x01, GGSTEPPER_GSTAT_CLEAR_VALUE);
  ggstepper_write_register(0x10, GGSTEPPER_IHOLD_IRUN_VALUE);
  ggstepper_write_register(0x13, GGSTEPPER_TPWMTHRS_VALUE);
  ggstepper_write_register(0x14, GGSTEPPER_TCOOLTHRS_VALUE);
  ggstepper_write_register(0x40, GGSTEPPER_SGTHRS_VALUE);
  ggstepper_write_register(0x42, GGSTEPPER_COOLCONF_VALUE);
  ggstepper_write_register(0x6C, chopconf);
  ggstepper_write_register(0x70, GGSTEPPER_PWMCONF_VALUE);
  ggstepper_write_register(0x22, GGSTEPPER_VACTUAL_VALUE);
}

/* DIAG edge (both edges), called from the EXTI1 interrupt. */
void ggstepper_diag_isr(void) {
  bool high = GGSTEPPER_DIAG_READ();
#if GGSTEPPER_DEBUG
  if (high && !ggstepper_diag_was_high && !ggstepper_moving) {
    if (ggstepper_debug_pending != 0xFF)
      ++ggstepper_debug_pending;
    ggstepper_debug_armed = ggstepper_diag_armed;
    ggstepper_debug_forward = ggstepper_current_forward;
    ggstepper_debug_sources |= 1;
    GGSTEPPER_STEP_LOW();
  }
  ggstepper_diag_was_high = high;
#endif
  if (ggstepper_diag_armed && high) {
    ggstepper_endpoint_hit = true;
    GGSTEPPER_STEP_LOW();
  }
}

static uint32_t ggstepper_clamp_u32(uint32_t value, uint32_t low,
                                    uint32_t high) {
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

static uint32_t ggstepper_isqrt_u32(uint32_t value) {
  uint32_t result = 0;
  uint32_t bit = 1UL << 30;
  while (bit > value)
    bit >>= 2;
  while (bit) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

static void ggstepper_motion_begin(uint32_t cruise_velocity,
                                   uint32_t acceleration) {
  uint32_t ticks = (2UL * GGSTEPPER_TIMER_TICKS_PER_SECOND +
                    cruise_velocity) / (2UL * cruise_velocity);
  if (ticks <= GGSTEPPER_STEP_HIGH_TICKS + 1UL)
    ticks = GGSTEPPER_STEP_HIGH_TICKS + 2UL;
  ggstepper_motion_speed_squared = 0;
  ggstepper_motion_velocity = 0;
  ggstepper_motion_cruise_velocity = cruise_velocity;
  ggstepper_motion_acceleration = acceleration;
  ggstepper_motion_cruise_squared = cruise_velocity * cruise_velocity;
  ggstepper_motion_cruise_ticks = ticks;
  ggstepper_motion_two_acceleration = 2UL * acceleration;
  ggstepper_motion_stopping_saturation =
      UINT32_MAX / ggstepper_motion_two_acceleration;
}

/* Called only between pulses. The squared-speed form avoids accumulated
   rounding while applying v^2 = u^2 + 2as for acceleration and braking.
   Values saturate where the exact value would exceed the cruise limit, so
   the result equals the unsaturated calculation. */
static uint32_t ggstepper_motion_next_period(uint32_t remaining,
                                             bool bounded) {
  uint32_t speed_squared = ggstepper_motion_speed_squared;
  uint32_t two_acceleration = ggstepper_motion_two_acceleration;
  uint32_t limit_squared = ggstepper_motion_cruise_squared;
  uint32_t next_squared;
  uint32_t start_velocity = ggstepper_motion_velocity;
  uint32_t end_velocity;
  uint32_t velocity_sum;
  uint32_t ticks;

  if (bounded) {
    uint32_t stopping_squared = 0;
    if (remaining > 0) {
      uint32_t stopping_steps = remaining - 1UL;
      stopping_squared =
          stopping_steps > ggstepper_motion_stopping_saturation
              ? UINT32_MAX
              : stopping_steps * two_acceleration;
    }
    if (stopping_squared < limit_squared)
      limit_squared = stopping_squared;
  }
  if (speed_squared == ggstepper_motion_cruise_squared &&
      limit_squared == speed_squared)
    return ggstepper_motion_cruise_ticks;
  next_squared = speed_squared > UINT32_MAX - two_acceleration
                     ? UINT32_MAX
                     : speed_squared + two_acceleration;
  if (next_squared > limit_squared)
    next_squared = limit_squared;
  ggstepper_motion_speed_squared = next_squared;
  end_velocity = ggstepper_isqrt_u32(next_squared);
  ggstepper_motion_velocity = end_velocity;
  velocity_sum = start_velocity + end_velocity;
  if (velocity_sum) {
    ticks = (2UL * GGSTEPPER_TIMER_TICKS_PER_SECOND +
             velocity_sum / 2UL) / velocity_sum;
  } else {
    /* A one-step triangular move accelerates and decelerates inside the step. */
    uint32_t peak_velocity =
        ggstepper_isqrt_u32(ggstepper_motion_acceleration);
    if (!peak_velocity)
      peak_velocity = 1;
    if (peak_velocity <= ggstepper_motion_cruise_velocity) {
      ticks = (2UL * GGSTEPPER_TIMER_TICKS_PER_SECOND +
               peak_velocity / 2UL) / peak_velocity;
    } else {
      /* Cruise is reached within this step: t = 1/v + v/a. */
      peak_velocity = ggstepper_motion_cruise_velocity;
      ticks = GGSTEPPER_TIMER_TICKS_PER_SECOND / peak_velocity +
          ((uint64_t)GGSTEPPER_TIMER_TICKS_PER_SECOND * peak_velocity +
           ggstepper_motion_acceleration / 2UL) /
              ggstepper_motion_acceleration;
    }
  }
  if (ticks <= GGSTEPPER_STEP_HIGH_TICKS + 1UL)
    ticks = GGSTEPPER_STEP_HIGH_TICKS + 2UL;
  return ticks;
}

static void ggstepper_async_disable(bool invalidate_position) {
  ggstepper_hw_timer_stop();
  GGSTEPPER_STEP_LOW();
  ggstepper_async_step_high = false;
  ggstepper_async_stop_requested = false;
  ggstepper_async_brake_requested = false;
  ggstepper_async_steps_remaining = 0;
  ggstepper_async_wait_ticks = 0;
  ggstepper_motion_speed_squared = 0;
  ggstepper_motion_velocity = 0;
  ggstepper_async_move_started = false;
  ggstepper_moving = false;
  if (invalidate_position)
    ggstepper_homed = false;
}

/* A slow step may need several timer cycles (the counter is 16 bits). Intermediate
   cycles do not pulse. The final timer ISR emits the bounded high pulse, lowers
   STEP before doing planner math, and installs the next rising-edge deadline. */
static bool ggstepper_async_schedule(uint32_t ticks) {
  const uint32_t chunk_ticks = 60000UL;
  uint16_t top;
  if (ticks > 65535UL) {
    ggstepper_async_wait_ticks = ticks - chunk_ticks;
    top = (uint16_t)(chunk_ticks - 1UL);
  } else {
    ggstepper_async_wait_ticks = 0;
    top = (uint16_t)(ticks - 1UL);
    if ((uint32_t)top <= ggstepper_hw_timer_count() +
                             GGSTEPPER_DEADLINE_MARGIN_TICKS)
      return false;
  }
  ggstepper_hw_timer_set_top(top);
  return true;
}

static void ggstepper_async_schedule_or_fault(uint32_t ticks) {
  if (!ggstepper_async_schedule(ticks)) {
    ggstepper_async_timer_fault = true;
    ggstepper_async_disable(true);
  }
}


static void ggstepper_async_move_done(void) {
  ggstepper_async_move_started = false;
  ggstepper_async_disable(false);
}

/* STEP timer interrupt (the former Timer1 compare-A ISR), called from the
   TIM3 update interrupt. */
void ggstepper_timer_isr(void) {
  if (ggstepper_async_stop_requested) {
    ggstepper_async_disable(true);
    return;
  }
  ggstepper_hw_timer_set_top(65535U);
  if (ggstepper_async_wait_ticks) {
    ggstepper_async_schedule_or_fault(ggstepper_async_wait_ticks);
    return;
  }
  if (!ggstepper_async_steps_remaining) {
    ggstepper_async_move_done();
    return;
  }

  GGSTEPPER_STEP_HIGH();
  ggstepper_async_step_high = true;
  delay_us(GGSTEPPER_STEP_HIGH_US);
  GGSTEPPER_STEP_LOW();
  ggstepper_async_step_high = false;
  ggstepper_async_move_started = true;
  ggstepper_position += ggstepper_async_forward ? 1 : -1;
  --ggstepper_async_steps_remaining;
  if (!ggstepper_async_steps_remaining) {
    ggstepper_async_move_done();
    return;
  }
  if (ggstepper_async_brake_requested) {
    uint32_t stopping_steps =
        (ggstepper_motion_speed_squared +
         2UL * ggstepper_motion_acceleration - 1UL) /
        (2UL * ggstepper_motion_acceleration);
    if (stopping_steps < ggstepper_async_steps_remaining) {
      ggstepper_async_steps_remaining = stopping_steps;
      ggstepper_async_target = ggstepper_position +
          (ggstepper_async_forward ? (int32_t)stopping_steps
                                   : -(int32_t)stopping_steps);
    }
    ggstepper_async_brake_requested = false;
    if (!ggstepper_async_steps_remaining) {
      ggstepper_async_move_done();
      return;
    }
  }
  ggstepper_async_schedule_or_fault(
      ggstepper_motion_next_period(ggstepper_async_steps_remaining, true));
}

static uint32_t ggstepper_contact_distance(int32_t a, int32_t b) {
  int64_t difference = (int64_t)a - b;
  if (difference < 0)
    difference = -difference;
  return (uint32_t)difference;
}

static void ggstepper_delay_ticks(uint32_t ticks) {
  uint32_t microseconds =
      (ticks + GGSTEPPER_TIMER_TICKS_PER_US - 1UL) /
      GGSTEPPER_TIMER_TICKS_PER_US;
  while (microseconds > 16000UL) {
    delay_us(16000UL);
    microseconds -= 16000UL;
  }
  if (microseconds)
    delay_us(microseconds);
}

static bool ggstepper_step_once(bool forward, uint32_t remaining,
                                bool bounded) {
  uint32_t planned_at = micros();
  uint32_t period_ticks = ggstepper_motion_next_period(remaining, bounded);
  uint32_t elapsed_ticks =
      (uint32_t)(micros() - planned_at) * GGSTEPPER_TIMER_TICKS_PER_US;
  uint32_t saved_primask;
  if (elapsed_ticks + GGSTEPPER_STEP_HIGH_TICKS >= period_ticks) {
    ggstepper_async_timer_fault = true;
    return false;
  }
  ggstepper_delay_ticks(
      period_ticks - GGSTEPPER_STEP_HIGH_TICKS - elapsed_ticks);
#if GGSTEPPER_DEBUG
  while (ggstepper_debug_pending)
    ggstepper_debug_service();
  ggstepper_current_forward = forward;
#endif
  saved_primask = ggstepper_irq_save();
  if (ggstepper_diag_armed && ggstepper_endpoint_hit) {
    ggstepper_irq_restore(saved_primask);
    return false;
  }
  GGSTEPPER_DIR_WRITE(!forward);
  GGSTEPPER_STEP_HIGH();
  ggstepper_irq_restore(saved_primask);
  delay_us(GGSTEPPER_STEP_HIGH_US);
  GGSTEPPER_STEP_LOW();
  ggstepper_position += forward ? 1 : -1;
#if GGSTEPPER_DEBUG
  while (ggstepper_debug_pending)
    ggstepper_debug_service();
#endif
  return true;
}

enum ggstepper_seek_result {
  GGSTEPPER_SEEK_CONTACT,
  GGSTEPPER_SEEK_UNCLEARED,
  GGSTEPPER_SEEK_FAILED
};

/* With TCOOLTHRS covering standstill, DIAG can stay high at rest and while
   ramping, because SG_RESULT is low at low velocity and holds its last value
   when stopped. DIAG is therefore ignored until homing cruise is reached and
   then armed only after it has been observed low at cruise. If it does not
   clear, contact may have occurred during the ramp; the caller backs off. */
static enum ggstepper_seek_result ggstepper_seek_contact(bool forward,
                                                         uint32_t max_steps,
                                                         int32_t *contact) {
  uint32_t moved = 0;
  uint32_t uncleared = 0;
  uint64_t cruise_squared =
      (uint64_t)GGSTEPPER_HOMING_VELOCITY * GGSTEPPER_HOMING_VELOCITY;
#if GGSTEPPER_DEBUG
  ggstepper_current_forward = forward;
#endif
  ggstepper_endpoint_hit = false;
  ggstepper_diag_armed = false;
  ggstepper_motion_begin(GGSTEPPER_HOMING_VELOCITY,
                         GGSTEPPER_HOMING_ACCELERATION);

  while (moved < max_steps) {
    if (!ggstepper_diag_armed &&
        ggstepper_motion_speed_squared >= cruise_squared &&
        moved >= ggstepper_diag_ignore_steps) {
      /* Arm before sampling so a rising edge after the sample reaches the ISR. */
      ggstepper_endpoint_hit = false;
      ggstepper_diag_armed = true;
      if (GGSTEPPER_DIAG_READ()) {
        ggstepper_diag_armed = false;
        ggstepper_endpoint_hit = false;
        if (++uncleared > GGSTEPPER_HOMING_DIAG_CLEAR_STEPS)
          break;
      }
    }
    if (!ggstepper_step_once(forward, 0, false)) {
      if (ggstepper_diag_armed && ggstepper_endpoint_hit) {
        ggstepper_diag_armed = false;
        if (contact)
          *contact = ggstepper_position;
#if GGSTEPPER_DEBUG
        while (ggstepper_debug_pending)
          ggstepper_debug_service();
#endif
        return GGSTEPPER_SEEK_CONTACT;
      }
      uncleared = 0;
      break;
    }
    ++moved;
  }

  ggstepper_diag_armed = false;
#if GGSTEPPER_DEBUG
  while (ggstepper_debug_pending)
    ggstepper_debug_service();
#endif
  return uncleared > GGSTEPPER_HOMING_DIAG_CLEAR_STEPS
             ? GGSTEPPER_SEEK_UNCLEARED
             : GGSTEPPER_SEEK_FAILED;
}

static bool ggstepper_move_steps(bool forward, uint32_t count) {
#if GGSTEPPER_DEBUG
  ggstepper_current_forward = forward;
#endif
  ggstepper_diag_armed = false;
  ggstepper_endpoint_hit = false;
  ggstepper_motion_begin(GGSTEPPER_HOMING_VELOCITY,
                         GGSTEPPER_HOMING_ACCELERATION);
  while (count) {
    if (!ggstepper_step_once(forward, count, true))
      return false;
    --count;
  }
  return true;
}

static bool ggstepper_before_candidate(bool forward, int32_t contact,
                                       int32_t candidate) {
  return forward
             ? (int64_t)contact <
                   (int64_t)candidate - GGSTEPPER_CONTACT_TOLERANCE_STEPS
             : (int64_t)contact >
                   (int64_t)candidate + GGSTEPPER_CONTACT_TOLERANCE_STEPS;
}

static bool ggstepper_confirmed_endpoint(bool forward, int32_t *endpoint) {
  int32_t candidate = 0;
  uint8_t matches = 0;
  uint8_t attempts = 0;

  while (attempts < GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS) {
    int32_t contact;
    enum ggstepper_seek_result result;
    if (attempts &&
        !ggstepper_move_steps(!forward, GGSTEPPER_CONTACT_BACKOFF_STEPS))
      return false;
    ++attempts;
    result = ggstepper_seek_contact(
        forward, GGSTEPPER_HOMING_MAX_SEEK_STEPS, &contact);
    if (result == GGSTEPPER_SEEK_UNCLEARED)
      continue;
    if (result != GGSTEPPER_SEEK_CONTACT)
      return false;
    if (!matches || ggstepper_before_candidate(forward, contact, candidate)) {
      candidate = contact;
      matches = 1;
    } else if (ggstepper_contact_distance(contact, candidate) <=
               GGSTEPPER_CONTACT_TOLERANCE_STEPS) {
      ++matches;
    } else {
      candidate = contact;
      matches = 1;
    }
    if (matches >= GGSTEPPER_CONTACT_CONFIRMATIONS) {
      *endpoint = candidate;
      return true;
    }
  }
  return false;
}

static bool ggstepper_move_to(int32_t target) {
#if GGSTEPPER_DEBUG
  ggstepper_current_forward = ggstepper_position < target;
#endif
  ggstepper_diag_armed = false;
  ggstepper_motion_begin(GGSTEPPER_HOMING_VELOCITY,
                         GGSTEPPER_HOMING_ACCELERATION);
  while (ggstepper_position != target) {
    uint32_t remaining = ggstepper_contact_distance(
        ggstepper_position, target);
    if (!ggstepper_step_once(ggstepper_position < target, remaining, true))
      return false;
  }
  return true;
}

void ggstepper_init(void) {
  /* Pins (driver disabled, STEP low), TIM3 and USART2. */
  ggstepper_hw_init();

  delay_ms(GGSTEPPER_DRIVER_SETTLE_MS);
#if GGSTEPPER_DEBUG
  ggstepper_diag_was_high = GGSTEPPER_DIAG_READ();
#endif
  ggstepper_hw_diag_interrupt_enable();

  ggstepper_configure_driver();

  ggstepper_hw_enable_driver(true);
  delay_ms(GGSTEPPER_ENABLE_WARMUP_MS);

  ggstepper_homed = false;
  ggstepper_moving = false;
  ggstepper_async_timer_fault = false;
  ggstepper_async_target = ggstepper_position;
}

bool ggstepper_home(void) {
  int32_t center;
  int64_t span;
  ggstepper_stop();
  ggstepper_homed = false;
  ggstepper_async_timer_fault = false;
  ggstepper_travel_steps = 0;
  if (!ggstepper_confirmed_endpoint(true, &ggstepper_max_position) ||
      !ggstepper_confirmed_endpoint(false, &ggstepper_min_position)) {
    ggstepper_async_target = ggstepper_position;
    return false;
  }
  span = (int64_t)ggstepper_max_position - ggstepper_min_position;
  if (span <= 0 || span > INT32_MAX) {
    ggstepper_async_target = ggstepper_position;
    return false;
  }
  ggstepper_travel_steps = (uint32_t)span;

  center = ggstepper_min_position + (int32_t)(ggstepper_travel_steps / 2);
  if (!ggstepper_move_to(center)) {
    ggstepper_async_target = ggstepper_position;
    return false;
  }

  ggstepper_min_position -= center;
  ggstepper_max_position -= center;
  ggstepper_position = 0;
  ggstepper_async_target = 0;
  ggstepper_homed = true;
  return true;
}

void ggstepper_poll(void) {
#if GGSTEPPER_DEBUG
  while (!ggstepper_moving && ggstepper_debug_pending)
    ggstepper_debug_service();
#endif
}

/* Starts the shared asynchronous executor toward a step target. */
static enum ggstepper_move_result ggstepper_start_move(
    int32_t target, uint32_t velocity, uint32_t acceleration) {
  int32_t position;
  uint32_t steps;
  uint32_t saved_primask;

  saved_primask = ggstepper_irq_save();
  if (ggstepper_moving) {
    ggstepper_irq_restore(saved_primask);
    return GGSTEPPER_MOVE_BUSY;
  }
  position = ggstepper_position;
  ggstepper_async_target = target;
  if (position == target) {
    ggstepper_irq_restore(saved_primask);
    return GGSTEPPER_MOVE_ALREADY_THERE;
  }

  ggstepper_async_forward = position < target;
  steps = (uint32_t)(ggstepper_async_forward
                         ? (int64_t)target - position
                         : (int64_t)position - target);
  ggstepper_async_steps_remaining = steps;
  ggstepper_async_stop_requested = false;
  ggstepper_async_brake_requested = false;
  ggstepper_async_step_high = false;
  ggstepper_async_timer_fault = false;
  ggstepper_moving = true;
  ggstepper_motion_begin(velocity, acceleration);
#if GGSTEPPER_DEBUG
  ggstepper_current_forward = ggstepper_async_forward;
#endif
  GGSTEPPER_DIR_WRITE(!ggstepper_async_forward);
  ggstepper_async_move_started = false;

  ggstepper_hw_timer_prepare();
  if (!ggstepper_async_schedule(ggstepper_motion_next_period(steps, true))) {
    ggstepper_async_timer_fault = true;
    ggstepper_async_disable(true);
    ggstepper_irq_restore(saved_primask);
    return GGSTEPPER_MOVE_INVALID;
  }
  ggstepper_hw_timer_start();
  ggstepper_irq_restore(saved_primask);
  return GGSTEPPER_MOVE_ACCEPTED;
}

enum ggstepper_move_result ggstepper_move_normalized(
    float value, int32_t *applied_position) {
  uint32_t offset;
  int32_t target;

  /* These comparisons reject NaN and both infinities as well as out-of-range
     finite values without depending on an AVR libm classification helper. */
  if (!(value >= 0.0F && value <= 1.0F))
    return GGSTEPPER_MOVE_INVALID;
  if (!ggstepper_homed)
    return GGSTEPPER_MOVE_NOT_HOMED;

  offset = (uint32_t)(value * (float)ggstepper_travel_steps + 0.5F);
  if (offset > ggstepper_travel_steps)
    offset = ggstepper_travel_steps;
  target = (int32_t)((int64_t)ggstepper_min_position + offset);
  if (applied_position)
    *applied_position = target;
  return ggstepper_start_move(target, ggstepper_profile_velocity,
                              ggstepper_profile_acceleration);
}

void ggstepper_get_status(struct ggstepper_status *status) {
  uint32_t saved_primask;
  uint32_t speed_squared;
  if (!status)
    return;
  saved_primask = ggstepper_irq_save();
  status->homed = ggstepper_homed;
  status->moving = ggstepper_moving;
  status->motion_fault = ggstepper_async_timer_fault;
  status->position = ggstepper_position;
  status->target = ggstepper_async_target;
  status->min_position = ggstepper_min_position;
  status->max_position = ggstepper_max_position;
  status->travel_steps = ggstepper_travel_steps;
  status->profile_velocity = ggstepper_profile_velocity;
  status->profile_acceleration = ggstepper_profile_acceleration;
  speed_squared = ggstepper_motion_speed_squared;
  ggstepper_irq_restore(saved_primask);
  status->current_velocity = ggstepper_isqrt_u32(speed_squared);
}

enum ggstepper_profile_result ggstepper_set_motion_profile(
    uint32_t requested_velocity, uint32_t requested_acceleration,
    struct ggstepper_motion_profile *applied) {
  uint32_t saved_primask;
  if (!requested_velocity || !requested_acceleration)
    return GGSTEPPER_PROFILE_INVALID;
  saved_primask = ggstepper_irq_save();
  if (ggstepper_moving) {
    ggstepper_irq_restore(saved_primask);
    return GGSTEPPER_PROFILE_BUSY;
  }
  ggstepper_profile_velocity = ggstepper_clamp_u32(
      requested_velocity, GGSTEPPER_PROFILE_MIN_VELOCITY,
      GGSTEPPER_PROFILE_MAX_VELOCITY);
  ggstepper_profile_acceleration = ggstepper_clamp_u32(
      requested_acceleration, GGSTEPPER_PROFILE_MIN_ACCELERATION,
      GGSTEPPER_PROFILE_MAX_ACCELERATION);
  if (applied) {
    applied->velocity_steps_per_second = ggstepper_profile_velocity;
    applied->acceleration_steps_per_second2 = ggstepper_profile_acceleration;
  }
  ggstepper_irq_restore(saved_primask);
  return GGSTEPPER_PROFILE_APPLIED;
}

void ggstepper_get_motion_profile(struct ggstepper_motion_profile *profile) {
  uint32_t saved_primask;
  if (!profile)
    return;
  saved_primask = ggstepper_irq_save();
  profile->velocity_steps_per_second = ggstepper_profile_velocity;
  profile->acceleration_steps_per_second2 = ggstepper_profile_acceleration;
  ggstepper_irq_restore(saved_primask);
}

void ggstepper_get_motion_limits(struct ggstepper_motion_limits *limits) {
  if (!limits)
    return;
  limits->min_velocity_steps_per_second = GGSTEPPER_PROFILE_MIN_VELOCITY;
  limits->max_velocity_steps_per_second = GGSTEPPER_PROFILE_MAX_VELOCITY;
  limits->min_acceleration_steps_per_second2 =
      GGSTEPPER_PROFILE_MIN_ACCELERATION;
  limits->max_acceleration_steps_per_second2 =
      GGSTEPPER_PROFILE_MAX_ACCELERATION;
}

bool ggstepper_brake(void) {
  uint32_t saved_primask = ggstepper_irq_save();
  if (!ggstepper_moving) {
    ggstepper_irq_restore(saved_primask);
    return false;
  }
  ggstepper_async_brake_requested = true;
  ggstepper_irq_restore(saved_primask);
  return true;
}

/* Immediate pulse abort. The home estimate is invalidated only if a move had
   emitted pulses and not finished; a stop during a dwell keeps position. */
void ggstepper_stop(void) {
  uint32_t saved_primask;
  bool invalidate;

  saved_primask = ggstepper_irq_save();
  if (!ggstepper_moving) {
    GGSTEPPER_STEP_LOW();
    ggstepper_irq_restore(saved_primask);
    return;
  }
  invalidate = ggstepper_async_move_started;
  ggstepper_async_target = ggstepper_position;
  ggstepper_async_disable(invalidate);
  ggstepper_irq_restore(saved_primask);
}


