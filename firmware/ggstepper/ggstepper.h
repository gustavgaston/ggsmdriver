#ifndef GGSTEPPER_H
#define GGSTEPPER_H

#include <stdbool.h>
#include <stdint.h>

/* Configuration for this board; see the comment at its top. */
#include "ggstepper_conf.h"

#define GGSTEPPER_STEPS_PER_REV \
  (GGSTEPPER_FULLSTEPS * GGSTEPPER_MICROSTEPS)

#ifndef GGSTEPPER_PROFILE_MIN_VELOCITY
#define GGSTEPPER_PROFILE_MIN_VELOCITY \
  ((GGSTEPPER_STEPS_PER_REV + 7UL) / 8UL)
#endif
#ifndef GGSTEPPER_PROFILE_MAX_VELOCITY
#define GGSTEPPER_PROFILE_MAX_VELOCITY GGSTEPPER_STEPS_PER_REV
#endif
#ifndef GGSTEPPER_PROFILE_DEFAULT_VELOCITY
#define GGSTEPPER_PROFILE_DEFAULT_VELOCITY \
  ((GGSTEPPER_STEPS_PER_REV + 1UL) / 2UL)
#endif
#ifndef GGSTEPPER_PROFILE_MIN_ACCELERATION
#define GGSTEPPER_PROFILE_MIN_ACCELERATION \
  ((GGSTEPPER_STEPS_PER_REV * 5UL + 15UL) / 16UL)
#endif
#ifndef GGSTEPPER_PROFILE_MAX_ACCELERATION
#define GGSTEPPER_PROFILE_MAX_ACCELERATION (GGSTEPPER_STEPS_PER_REV * 8UL)
#endif
#ifndef GGSTEPPER_PROFILE_DEFAULT_ACCELERATION
#define GGSTEPPER_PROFILE_DEFAULT_ACCELERATION GGSTEPPER_STEPS_PER_REV
#endif
#ifndef GGSTEPPER_HOMING_VELOCITY
#define GGSTEPPER_HOMING_VELOCITY \
  (GGSTEPPER_STEPS_PER_REV * GGSTEPPER_RPS)
#endif
#ifndef GGSTEPPER_HOMING_ACCELERATION
#define GGSTEPPER_HOMING_ACCELERATION (GGSTEPPER_STEPS_PER_REV * 4UL)
#endif
#ifndef GGSTEPPER_HOMING_MAX_SEEK_STEPS
#define GGSTEPPER_HOMING_MAX_SEEK_STEPS (GGSTEPPER_STEPS_PER_REV * 10UL)
#endif
#ifndef GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS
#define GGSTEPPER_HOMING_MAX_CONTACT_ATTEMPTS 8U
#endif
/* Steps at homing cruise allowed for DIAG to read low before the seek is
   treated as possible contact during the ramp and retried after backoff. */
#ifndef GGSTEPPER_HOMING_DIAG_CLEAR_STEPS
#define GGSTEPPER_HOMING_DIAG_CLEAR_STEPS ((GGSTEPPER_STEPS_PER_REV + 7UL) / 8UL)
#endif

extern volatile int32_t ggstepper_position;
extern int32_t ggstepper_min_position;
extern int32_t ggstepper_max_position;
extern uint32_t ggstepper_travel_steps;

enum ggstepper_move_result {
  GGSTEPPER_MOVE_ACCEPTED,
  GGSTEPPER_MOVE_ALREADY_THERE,
  GGSTEPPER_MOVE_INVALID,
  GGSTEPPER_MOVE_NOT_HOMED,
  GGSTEPPER_MOVE_BUSY
};

struct ggstepper_status {
  bool homed;
  bool moving;
  bool motion_fault;
  int32_t position;
  int32_t target;
  int32_t min_position;
  int32_t max_position;
  uint32_t travel_steps;
  uint32_t profile_velocity;
  uint32_t profile_acceleration;
  uint32_t current_velocity;
};

struct ggstepper_motion_profile {
  uint32_t velocity_steps_per_second;
  uint32_t acceleration_steps_per_second2;
};

struct ggstepper_motion_limits {
  uint32_t min_velocity_steps_per_second;
  uint32_t max_velocity_steps_per_second;
  uint32_t min_acceleration_steps_per_second2;
  uint32_t max_acceleration_steps_per_second2;
};

enum ggstepper_profile_result {
  GGSTEPPER_PROFILE_APPLIED,
  GGSTEPPER_PROFILE_BUSY,
  GGSTEPPER_PROFILE_INVALID
};

void ggstepper_init(void);
bool ggstepper_home(void);
void ggstepper_poll(void);
enum ggstepper_profile_result ggstepper_set_motion_profile(
    uint32_t requested_velocity, uint32_t requested_acceleration,
    struct ggstepper_motion_profile *applied);
void ggstepper_get_motion_profile(struct ggstepper_motion_profile *profile);
void ggstepper_get_motion_limits(struct ggstepper_motion_limits *limits);
enum ggstepper_move_result ggstepper_move_normalized(
    float value, int32_t *applied_position);
void ggstepper_get_status(struct ggstepper_status *status);
bool ggstepper_brake(void);
void ggstepper_stop(void);

/* Core only: sequences and calibration are not ported yet. */
#ifndef GGSTEPPER_SEQUENCE_CAPACITY
#define GGSTEPPER_SEQUENCE_CAPACITY 0
#endif
#ifndef GGSTEPPER_CALIBRATION
#define GGSTEPPER_CALIBRATION 0
#endif
#if GGSTEPPER_SEQUENCE_CAPACITY || GGSTEPPER_CALIBRATION
#error "ggstepper sequences and calibration are not part of the STM32 port yet"
#endif

#endif
