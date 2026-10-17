#ifndef GGSTEPPER_AUTOMATO_H
#define GGSTEPPER_AUTOMATO_H

#define GGSTEPPER_AUTOMATO_MIDDLEWARE "ggstepper"

#include <math.h>
#include <stddef.h>

#include "automato_bridge.h"
#include "ggstepper.h"
#include "serial.h"

/* Automato adapter: two controls for the (single) ggstepper of a sketch.

   position: 0..1 of the homed range (rest 0.5). A move runs to completion
     with the current speed and the acceleration below; a new position that
     arrives while a move runs is kept and started when the move ends (latest
     wins) until ggstepper can redirect a running move. Settled = homed, not
     moving and nothing pending.
   speed: 0..1 maps linearly onto the configured velocity range
     GGSTEPPER_PROFILE_MIN_VELOCITY..MAX_VELOCITY (rest = the default
     velocity) and applies to moves that start after it is set.

     static const struct automato_control CONTROLS[] = {
       GGSTEPPER_AUTOMATO_POSITION("ggstepper1.position"),
       GGSTEPPER_AUTOMATO_SPEED("ggstepper1.speed"),
     };

   Call ggstepper_init() and ggstepper_home() before automato_begin(), and
   ggstepper_poll() in the main loop. This header holds the adapter's function
   definitions: include it from exactly one source file (main.c). Per-startup measurements (the homed travel)
   are reported under "measured" so they do not change the setup's identity. */

/* Acceleration for every move; the configurator's validated maximum by
   default. A sketch may define a lower value before including this header. */
#ifndef GGSTEPPER_AUTOMATO_ACCELERATION
#define GGSTEPPER_AUTOMATO_ACCELERATION GGSTEPPER_PROFILE_MAX_ACCELERATION
#endif
#ifndef GGSTEPPER_PROFILE_CALIBRATED
#define GGSTEPPER_PROFILE_CALIBRATED 0
#endif

#define GGSTEPPER_AUTOMATO_VELOCITY_SPAN \
  ((uint32_t)GGSTEPPER_PROFILE_MAX_VELOCITY - (uint32_t)GGSTEPPER_PROFILE_MIN_VELOCITY)
#define GGSTEPPER_AUTOMATO_SPEED_REST                                          \
  ((uint16_t)((((uint32_t)GGSTEPPER_PROFILE_DEFAULT_VELOCITY -                 \
                (uint32_t)GGSTEPPER_PROFILE_MIN_VELOCITY) *                    \
                   (uint32_t)AUTOMATO_VALUE_MAX +                              \
               GGSTEPPER_AUTOMATO_VELOCITY_SPAN / 2UL) /                       \
              GGSTEPPER_AUTOMATO_VELOCITY_SPAN))

static uint16_t ggstepper_automato_target = AUTOMATO_VALUE_MAX / 2U;
static bool ggstepper_automato_pending = false;
static uint16_t ggstepper_automato_speed = GGSTEPPER_AUTOMATO_SPEED_REST;

static uint32_t ggstepper_automato_velocity(void) {
  return (uint32_t)GGSTEPPER_PROFILE_MIN_VELOCITY +
         (uint32_t)(((uint64_t)ggstepper_automato_speed * GGSTEPPER_AUTOMATO_VELOCITY_SPAN +
                     AUTOMATO_VALUE_MAX / 2U) / AUTOMATO_VALUE_MAX);
}

/* Starts the pending position when the motor is free. */
static void ggstepper_automato_poll(void) {
  struct ggstepper_status status;
  enum ggstepper_move_result result;
  if (!ggstepper_automato_pending)
    return;
  ggstepper_get_status(&status);
  if (status.moving)
    return;
  if (!status.homed) {
    ggstepper_automato_pending = false;
    return;
  }
  ggstepper_set_motion_profile(ggstepper_automato_velocity(),
                               GGSTEPPER_AUTOMATO_ACCELERATION, NULL);
  result = ggstepper_move_normalized(
      (float)ggstepper_automato_target / (float)AUTOMATO_VALUE_MAX, NULL);
  if (result != GGSTEPPER_MOVE_BUSY)
    ggstepper_automato_pending = false;
}

static void ggstepper_automato_set_position(uint16_t value) {
  ggstepper_automato_target = value;
  ggstepper_automato_pending = true;
  ggstepper_automato_poll();
}

static uint16_t ggstepper_automato_position(void) {
  return ggstepper_automato_target;
}

static bool ggstepper_automato_position_settled(void) {
  struct ggstepper_status status;
  ggstepper_get_status(&status);
  return status.homed && !status.moving && !ggstepper_automato_pending;
}

static void ggstepper_automato_stop(void) {
  ggstepper_automato_pending = false;
  ggstepper_brake();
}

static double ggstepper_automato_fraction(const struct ggstepper_status *status,
                                          int32_t steps) {
  if (!status->travel_steps)
    return 0.0;
  if (steps <= status->min_position)
    return 0.0;
  if (steps >= status->max_position)
    return 1.0;
  return (double)(steps - status->min_position) / (double)status->travel_steps;
}

/* Time for a full-range move at a velocity (trapezoid, from rest to rest). */
static double ggstepper_automato_travel_s(const struct ggstepper_status *status,
                                          uint32_t velocity) {
  double travel = (double)status->travel_steps;
  double ramp = (double)velocity * (double)velocity / (double)GGSTEPPER_AUTOMATO_ACCELERATION;
  if (travel <= ramp)
    return 2.0 * sqrt(travel / (double)GGSTEPPER_AUTOMATO_ACCELERATION);
  return travel / (double)velocity + (double)velocity / (double)GGSTEPPER_AUTOMATO_ACCELERATION;
}

static void ggstepper_automato_print_position_capability(void) {
  struct ggstepper_status status;
  ggstepper_get_status(&status);
  serial_print("\"middleware\":\"" GGSTEPPER_AUTOMATO_MIDDLEWARE "\",");
  serial_print("\"value_meaning\":\"fraction of the homed range\",");
  serial_print("\"acceleration_steps_per_second2\":");
  serial_print((uint32_t)GGSTEPPER_AUTOMATO_ACCELERATION);
  serial_print(",\"calibrated\":");
  automato_print_bool(GGSTEPPER_PROFILE_CALIBRATED);
  serial_print(",\"hints\":{\"slew_s\":");
  serial_print_float(ggstepper_automato_travel_s(&status, GGSTEPPER_PROFILE_DEFAULT_VELOCITY), 3);
  serial_print(",\"slew_s_fastest\":");
  serial_print_float(ggstepper_automato_travel_s(&status, GGSTEPPER_PROFILE_MAX_VELOCITY), 3);
  serial_print(",\"slew_s_slowest\":");
  serial_print_float(ggstepper_automato_travel_s(&status, GGSTEPPER_PROFILE_MIN_VELOCITY), 3);
  serial_print("},\"measured\":{\"homed\":");
  automato_print_bool(status.homed);
  serial_print(",\"travel_steps\":");
  serial_print(status.travel_steps);
  serial_print(",\"resolution\":");
  if (status.travel_steps)
    serial_print_float(1.0 / (double)status.travel_steps, 8);
  else
    serial_print("null");
  serial_print("}");
}

static void ggstepper_automato_print_position_state(void) {
  struct ggstepper_status status;
  ggstepper_get_status(&status);
  serial_print("\"measured\":");
  serial_print_float(ggstepper_automato_fraction(&status, status.position), 6);
  serial_print(",\"homed\":");
  automato_print_bool(status.homed);
  serial_print(",\"moving\":");
  automato_print_bool(status.moving);
  serial_print(",\"pending\":");
  automato_print_bool(ggstepper_automato_pending);
  serial_print(",\"fault\":");
  automato_print_bool(status.motion_fault);
  serial_print(",\"position_steps\":");
  serial_print(status.position);
  serial_print(",\"target_steps\":");
  serial_print(status.target);
  serial_print(",\"velocity_steps_per_second\":");
  serial_print(status.current_velocity);
}

static void ggstepper_automato_set_speed(uint16_t value) {
  ggstepper_automato_speed = value;
}

static uint16_t ggstepper_automato_speed_value(void) {
  return ggstepper_automato_speed;
}

static bool ggstepper_automato_speed_settled(void) {
  return true;
}

static void ggstepper_automato_speed_stop(void) {
}

static void ggstepper_automato_print_speed_capability(void) {
  serial_print("\"middleware\":\"" GGSTEPPER_AUTOMATO_MIDDLEWARE "\",");
  serial_print("\"value_meaning\":\"move velocity, linear in the configured range; "
               "applies to moves that start after it is set\",");
  serial_print("\"resolution\":");
  serial_print_float(1.0 / (double)GGSTEPPER_AUTOMATO_VELOCITY_SPAN, 8);
  serial_print(",\"velocity_range_steps_per_second\":[");
  serial_print((uint32_t)GGSTEPPER_PROFILE_MIN_VELOCITY);
  serial_print(",");
  serial_print((uint32_t)GGSTEPPER_PROFILE_MAX_VELOCITY);
  serial_print("],\"hints\":{}");
}

static void ggstepper_automato_print_speed_state(void) {
  serial_print("\"measured\":null,\"velocity_steps_per_second\":");
  serial_print(ggstepper_automato_velocity());
}

#define GGSTEPPER_AUTOMATO_POSITION(control_id)                                  \
  {(control_id), AUTOMATO_VALUE_MAX / 2U, ggstepper_automato_set_position,      \
   ggstepper_automato_position, ggstepper_automato_position_settled,           \
   ggstepper_automato_stop, ggstepper_automato_poll,                           \
   ggstepper_automato_print_position_capability,                               \
   ggstepper_automato_print_position_state}

#define GGSTEPPER_AUTOMATO_SPEED(control_id)                                     \
  {(control_id), GGSTEPPER_AUTOMATO_SPEED_REST, ggstepper_automato_set_speed,   \
   ggstepper_automato_speed_value, ggstepper_automato_speed_settled,           \
   ggstepper_automato_speed_stop, NULL,                                         \
   ggstepper_automato_print_speed_capability,                                  \
   ggstepper_automato_print_speed_state}

#endif
