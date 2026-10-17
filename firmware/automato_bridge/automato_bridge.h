#ifndef AUTOMATO_BRIDGE_H
#define AUTOMATO_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Automato serial bridge (protocol automato.serial.v2).

   A device is a list of controls, each set between 0.0 and 1.0. Middleware
   adapters (e.g. ggfan_automato.h, ggstepper_automato.h) provide the
   controls and translate values into hardware actions within the limits of
   the device's configuration headers; the bridge knows nothing about them.
   The device sketch lists its controls and calls automato_begin() and
   automato_poll():

     static const struct automato_control CONTROLS[] = {GGFAN_AUTOMATO_CONTROL("ggfan1")};
     ... init the controls' hardware; automato_begin("my_device", CONTROLS, 1);
     ... then call automato_poll() from the main loop.

   Values travel as decimals 0..1 and are stored as 0..65535. Sequences run
   on a fixed schedule from the start tick: an entry's values are set at its
   start, then it holds for dwell_us; a program lasts sum(dwell) x repeat. A
   control that has not caught up when a new value arrives is its adapter's
   business. The executor runs from loop(); the status reports the largest
   lateness. When a program ends, the bridge prints an event line by itself
   so the host need not poll while it plays.

   Commands: DESCRIBE | STATUS | SET id value | REST | STOP |
   SEQ BEGIN seq_id repeat | SEQ ADD dwell_us [control=value ...] | SEQ END |
   SEQ START seq_id | SEQ STATUS. `control` is a control id or its index. */

#ifndef AUTOMATO_MAX_CONTROLS
#define AUTOMATO_MAX_CONTROLS 8
#endif
/* Sequence entries, and the value settings they hold in total. */
#ifndef AUTOMATO_SEQUENCE_CAPACITY
#define AUTOMATO_SEQUENCE_CAPACITY 48
#endif
#ifndef AUTOMATO_SEQUENCE_SETS
#define AUTOMATO_SEQUENCE_SETS 96
#endif
#ifndef AUTOMATO_COMMAND_LENGTH
#define AUTOMATO_COMMAND_LENGTH 127
#endif
/* Resolution of the micros() clock the descriptor advertises. The AVR build
   reported 4 (Timer0 granularity); this board's micros() counts 1 us ticks
   (board_time.h, TIM17 at 1 MHz). The only wire difference from the AVR bridge. */
#ifndef AUTOMATO_CLOCK_RESOLUTION_US
#define AUTOMATO_CLOCK_RESOLUTION_US 1
#endif
#if AUTOMATO_SEQUENCE_CAPACITY > 255 || AUTOMATO_SEQUENCE_SETS > 255
#error "AUTOMATO_SEQUENCE_CAPACITY and AUTOMATO_SEQUENCE_SETS must be at most 255"
#endif
#define AUTOMATO_SEQUENCE_MAX_REPEAT 65535UL
#define AUTOMATO_SEQUENCE_MAX_DWELL_US 60000000UL
#define AUTOMATO_VALUE_MAX 65535U

struct automato_control {
  const char *id;                 /* unique on the device, e.g. "ggstepper1.position" */
  uint16_t rest;                  /* value at rest (REST, and before every recording) */
  void (*set)(uint16_t value);    /* apply a value 0..65535 (= 0.0..1.0) */
  uint16_t (*commanded)(void);    /* the value last applied */
  bool (*settled)(void);          /* physically at the commanded state */
  void (*stop)(void);             /* stop safely (sequence aborted, STOP) */
  void (*poll)(void);             /* optional work from loop(); may be NULL */
  void (*print_capability)(void); /* JSON fields: middleware, resolution, hints, limits */
  void (*print_state)(void);      /* JSON fields: measurements, incl. "measured" 0..1 */
};

void automato_begin(const char *device, const struct automato_control *controls,
                    uint8_t count);
void automato_poll(void);
/* Prints the descriptor again. A real Arduino board prints it after the reset
   that opening its serial port causes; over USB CDC nothing resets, so the
   firmware calls this when the host opens the port (serial_take_open_event()). */
void automato_announce(void);
/* Helpers for adapters printing JSON values. */
void automato_print_value(uint16_t value);
void automato_print_bool(bool value);

#endif
