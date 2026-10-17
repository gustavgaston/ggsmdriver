/* ggstepper configuration for the GGSMDriver board (STM32G0B1CBT6).

   Values are copied unchanged from the Nodharmonica bridge configuration,
   devices/nodharmonica/nodharmonica_automato/ggstepper_conf.h in the Automato
   repository (and its GGSTEPPER_DEBUG 0 from nodharmonica_automato.ino), as
   asked by the owner. Only the AVR-specific pin and register macros were
   replaced (the pin map is in board_pins.h) and the settings that do not
   exist here were dropped (GGSTEPPER_DEBUG_BAUD: debug output goes to the USB
   serial port; the UART_RX/TX pin and DIAG interrupt register macros).

   NOT re-tuned for this board: IRUN/IHOLD/VSENSE, the profile bounds
   (validated on the Uno with the Nodharmonica motor and load) and the homing
   constants. See README.md. */
#ifndef GGSTEPPER_CONF_H
#define GGSTEPPER_CONF_H

/* Scope of this port: core only. */
#define GGSTEPPER_SEQUENCE_CAPACITY 0
#define GGSTEPPER_CALIBRATION 0

/* 1: report every DIAG edge as a text line on the USB serial port (the line is not
   Automato JSON and the host rejects it at startup, as in the AVR build; keep 0 for
   the bridge). Build with `make EXTRA_CFLAGS=-DGGSTEPPER_DEBUG=1` to try it. */
#ifndef GGSTEPPER_DEBUG
#define GGSTEPPER_DEBUG 0
#endif
#define GGSTEPPER_UART_READ_TIMEOUT_MS 20UL
#define GGSTEPPER_FULLSTEPS 200UL
#define GGSTEPPER_MICROSTEPS 16UL
#define GGSTEPPER_RPS 1UL
/* MAX values are the finder's validated, derated operating profile for this setup and load. */
#define GGSTEPPER_PROFILE_CALIBRATED 1
#define GGSTEPPER_PROFILE_MIN_VELOCITY 400UL
#define GGSTEPPER_PROFILE_MAX_VELOCITY 5400UL
#define GGSTEPPER_PROFILE_DEFAULT_VELOCITY 1600UL
#define GGSTEPPER_PROFILE_MIN_ACCELERATION 1000UL
#define GGSTEPPER_PROFILE_MAX_ACCELERATION 240000UL
#define GGSTEPPER_PROFILE_DEFAULT_ACCELERATION 3200UL
#define GGSTEPPER_HOMING_VELOCITY 3200UL
#define GGSTEPPER_HOMING_ACCELERATION 12800UL
#define GGSTEPPER_HOMING_MAX_SEEK_STEPS 32000UL
#define GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS 8
#define GGSTEPPER_IRUN 3
#define GGSTEPPER_IHOLD 3
#define GGSTEPPER_VSENSE 1
#define GGSTEPPER_CONTACT_BACKOFF_STEPS 800UL
#define GGSTEPPER_CONTACT_TOLERANCE_STEPS 32L
#define GGSTEPPER_CONTACT_CONFIRMATIONS 2
#define GGSTEPPER_DIAG_IGNORE_MS 0UL
#define GGSTEPPER_DRIVER_SETTLE_MS 200UL
#define GGSTEPPER_ENABLE_WARMUP_MS 150UL
#define GGSTEPPER_STEP_HIGH_US 5UL
#define GGSTEPPER_DRIVER_ADDRESS 0
#define GGSTEPPER_UART_BAUD 19200UL
#define GGSTEPPER_SGTHRS_VALUE 100
#define GGSTEPPER_GCONF_VALUE 0x000001C0UL
#define GGSTEPPER_GSTAT_CLEAR_VALUE 0x00000007UL
#define GGSTEPPER_IHOLDDELAY 1
#define GGSTEPPER_TPWMTHRS_VALUE 0x00000000UL
#define GGSTEPPER_TCOOLTHRS_VALUE 0x000FFFFFUL
#define GGSTEPPER_COOLCONF_VALUE 0x00000000UL
#define GGSTEPPER_CHOPCONF_BASE 0x10000053UL
#define GGSTEPPER_TOFF 4
#define GGSTEPPER_PWMCONF_VALUE 0xC10D0024UL
#define GGSTEPPER_VACTUAL_VALUE 0x00000000UL

/* ---- Executor (this port, not from the Nodharmonica configuration) ----
   The STEP timer (TIM3) ticks at 1 MHz. Both are executor representability
   limits, not motor limits, and not measured on hardware. */
#define GGSTEPPER_TIMER_TICKS_PER_US 1UL
/* Deadline margin for the STEP timer: a new interval must end more than this
   many ticks after the current count, otherwise the move is stopped and the
   position invalidated. The AVR build used 8 ticks of 0.5 us (4 us). */
#define GGSTEPPER_DEADLINE_MARGIN_TICKS 4UL /* TODO(verify): measure interrupt latency + planner time on hardware */

/*
 * Velocity/acceleration finder 2026-10-02T10:31:56Z: validated.
 * Setup: not recorded
 * Reference spread 0 steps; lost-step tolerance +/-32 steps; long stroke 3516, short stroke 439 steps.
 * StallGuard references are coarse evidence, not an encoder.
 * phase     v rev/s  a rev/s2  stroke  cycles  peak rev/s  outcome  offsets F/R  diag
 * baseline        1         4    3516      10           1  pass  0/0  62
 * search        1.5         4    3516      10         1.5  pass  0/0  66
 * search        1.5         6    3516      10         1.5  pass  0/0  50
 * search       2.25         6    3516      10        2.25  pass  15/0  51
 * search       2.25         9    3516      10        2.25  pass  0/0  28
 * search       2.25      13.5    3516      10        2.25  pass  0/0  22
 * search       3.38      13.5    3516      10        3.38  executor_limit  0/0  0
 * search       2.25      20.2    3516      10        2.25  pass  0/0  20
 * search       2.25      30.4    3516      10        2.25  pass  0/0  20
 * search       2.25      45.6    3516      10        2.25  pass  0/0  20
 * search       2.25      68.3    3516      10        2.25  pass  0/0  20
 * search       2.25       100    3516      10        2.25  pass  0/0  20
 * validate     1.69        75    3516      10        1.69  pass  0/0  20
 * validate     1.69        75    3516      10        1.69  pass  0/0  20
 * validate     1.69        75    3516      10        1.69  pass  0/0  20
 * validate     1.69        75     439      20        1.69  pass  0/0  39
 */

#endif
