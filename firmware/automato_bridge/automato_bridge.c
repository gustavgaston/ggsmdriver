#include "automato_bridge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "board_time.h"
#include "serial.h"

enum automato_sequence_state {
  AUTOMATO_SEQUENCE_EMPTY,
  AUTOMATO_SEQUENCE_LOADING,
  AUTOMATO_SEQUENCE_LOADED,
  AUTOMATO_SEQUENCE_RUNNING,
  AUTOMATO_SEQUENCE_COMPLETED,
  AUTOMATO_SEQUENCE_ABORTED
};

struct automato_entry {
  uint32_t dwell_us;
  uint8_t first_set;
  uint8_t set_count;
};

struct automato_setting {
  uint8_t control;
  uint16_t value;
};

static const char *automato_device = "";
static const struct automato_control *automato_controls = NULL;
static uint8_t automato_control_count = 0;

static char automato_command[AUTOMATO_COMMAND_LENGTH + 1];
static uint8_t automato_command_length = 0;
static bool automato_command_overflow = false;

static struct automato_entry automato_entries[AUTOMATO_SEQUENCE_CAPACITY];
static struct automato_setting automato_settings[AUTOMATO_SEQUENCE_SETS];
static enum automato_sequence_state automato_state = AUTOMATO_SEQUENCE_EMPTY;
static uint32_t automato_sequence_id = 0;
static uint8_t automato_entry_count = 0;
static uint8_t automato_setting_count = 0;
static uint32_t automato_repeat = 0;
static uint8_t automato_entry = 0;
static uint32_t automato_repetition = 0;
static uint32_t automato_start_us = 0;
static uint32_t automato_entry_start_us = 0;
static uint32_t automato_end_us = 0;
static uint32_t automato_max_late_us = 0;
static bool automato_stopped = false;

/* ------------------------------------------------------------ JSON output */

void automato_print_value(uint16_t value) {
  serial_print_float((double)value / (double)AUTOMATO_VALUE_MAX, 6);
}

void automato_print_bool(bool value) {
  serial_print(value ? "true" : "false");
}

static void automato_print_error(const char *code,
                                 const char *message) {
  serial_print("{\"type\":\"error\",\"code\":\"");
  serial_print(code);
  serial_print("\",\"message\":\"");
  serial_print(message);
  serial_println("\"}");
}

static const char *automato_state_name(void) {
  switch (automato_state) {
    case AUTOMATO_SEQUENCE_LOADING: return "loading";
    case AUTOMATO_SEQUENCE_LOADED: return "loaded";
    case AUTOMATO_SEQUENCE_RUNNING: return "running";
    case AUTOMATO_SEQUENCE_COMPLETED: return "completed";
    case AUTOMATO_SEQUENCE_ABORTED: return "aborted";
    default: return "empty";
  }
}

static void automato_print_controls(void) {
  uint8_t index;
  serial_print("\"controls\":[");
  for (index = 0; index < automato_control_count; ++index) {
    const struct automato_control *control = &automato_controls[index];
    if (index)
      serial_print(",");
    serial_print("{\"id\":\"");
    serial_print(control->id);
    serial_print("\",\"value\":");
    automato_print_value(control->commanded());
    serial_print(",\"settled\":");
    automato_print_bool(control->settled());
    serial_print(",");
    control->print_state();
    serial_print("}");
  }
  serial_print("]");
}

static void automato_print_head(const char *type) {
  serial_print("{\"type\":\"");
  serial_print(type);
  serial_print("\",\"device\":\"");
  serial_print(automato_device);
  serial_print("\",");
}

static void automato_print_status(const char *type) {
  automato_print_head(type);
  serial_print("\"sequence_state\":\"");
  serial_print(automato_state_name());
  serial_print("\",");
  automato_print_controls();
  serial_println("}");
}

static void automato_print_descriptor(void) {
  uint8_t index;
  serial_print("{\"type\":\"descriptor\",\"protocol\":\"automato.serial.v2\",\"device\":\"");
  serial_print(automato_device);
  serial_print("\",\"controls\":[");
  for (index = 0; index < automato_control_count; ++index) {
    const struct automato_control *control = &automato_controls[index];
    if (index)
      serial_print(",");
    serial_print("{\"index\":");
    serial_print(index);
    serial_print(",\"id\":\"");
    serial_print(control->id);
    serial_print("\",\"rest\":");
    automato_print_value(control->rest);
    serial_print(",");
    control->print_capability();
    serial_print("}");
  }
  serial_print("],\"sequence\":{\"capacity\":");
  serial_print(AUTOMATO_SEQUENCE_CAPACITY);
  serial_print(",\"sets\":");
  serial_print(AUTOMATO_SEQUENCE_SETS);
  serial_print(",\"max_repeat\":");
  serial_print(AUTOMATO_SEQUENCE_MAX_REPEAT);
  serial_print(",\"max_dwell_us\":");
  serial_print(AUTOMATO_SEQUENCE_MAX_DWELL_US);
  serial_print(",\"value_steps\":");
  serial_print(AUTOMATO_VALUE_MAX);
  serial_print(",\"timing\":\"fixed_schedule\",\"clock\":\"micros\",");
  serial_print("\"clock_resolution_us\":");
  serial_print(AUTOMATO_CLOCK_RESOLUTION_US);
  serial_print(",\"executor\":\"loop_poll\",");
  serial_println("\"timing_verified\":false}}");
}

static void automato_print_sequence(const char *type,
                                    const char *event, bool controls) {
  automato_print_head(type);
  serial_print("\"event\":\"");
  serial_print(event);
  serial_print("\",\"now_us\":");
  serial_print(micros());
  serial_print(",\"sequence_state\":\"");
  serial_print(automato_state_name());
  serial_print("\",\"sequence_id\":");
  serial_print(automato_sequence_id);
  serial_print(",\"entries\":");
  serial_print(automato_entry_count);
  serial_print(",\"sets\":");
  serial_print(automato_setting_count);
  serial_print(",\"repeat\":");
  serial_print(automato_repeat);
  serial_print(",\"entry\":");
  serial_print(automato_entry);
  serial_print(",\"repetition\":");
  serial_print(automato_repetition);
  serial_print(",\"start_us\":");
  serial_print(automato_start_us);
  serial_print(",\"end_us\":");
  serial_print(automato_end_us);
  serial_print(",\"max_late_us\":");
  serial_print(automato_max_late_us);
  serial_print(",\"stopped\":");
  automato_print_bool(automato_stopped);
  if (controls) {
    serial_print(",");
    automato_print_controls();
  }
  serial_println("}");
}

/* ------------------------------------------------------------ parsing */

static char *automato_skip_space(char *text) {
  while (*text == ' ' || *text == '\t')
    ++text;
  return text;
}

static bool automato_only_space(const char *text) {
  return *automato_skip_space((char *)text) == '\0';
}

/* Next whitespace-separated token, NUL-terminated in place. */
static char *automato_token(char **cursor) {
  char *start = automato_skip_space(*cursor);
  char *end = start;
  if (!*start)
    return NULL;
  while (*end && *end != ' ' && *end != '\t')
    ++end;
  if (*end)
    *end++ = '\0';
  *cursor = end;
  return start;
}

static bool automato_parse_u32(const char *text, uint32_t *value) {
  char *end;
  unsigned long parsed;
  if (!text || *text < '0' || *text > '9')
    return false;
  parsed = strtoul(text, &end, 10);
  if (*end)
    return false;
  *value = (uint32_t)parsed;
  return true;
}

static bool automato_parse_value(const char *text, uint16_t *value) {
  char *end;
  double parsed;
  if (!text || !*text)
    return false;
  parsed = strtod(text, &end);
  if (*end || isnan(parsed) || parsed < 0.0 || parsed > 1.0)
    return false;
  *value = (uint16_t)(parsed * (double)AUTOMATO_VALUE_MAX + 0.5);
  return true;
}

/* A control by id, or by its index written as digits. */
static int16_t automato_find(const char *name) {
  uint32_t index;
  uint8_t candidate;
  for (candidate = 0; candidate < automato_control_count; ++candidate)
    if (strcmp(automato_controls[candidate].id, name) == 0)
      return candidate;
  if (automato_parse_u32(name, &index) && index < automato_control_count)
    return (int16_t)index;
  return -1;
}

/* ------------------------------------------------------------ sequencer */

static void automato_clear(void) {
  automato_state = AUTOMATO_SEQUENCE_EMPTY;
  automato_entry_count = 0;
  automato_setting_count = 0;
}

static void automato_apply(uint8_t entry) {
  uint8_t index;
  const struct automato_entry *current = &automato_entries[entry];
  for (index = 0; index < current->set_count; ++index) {
    const struct automato_setting *setting = &automato_settings[current->first_set + index];
    automato_controls[setting->control].set(setting->value);
  }
}

static void automato_abort(void) {
  uint8_t index;
  if (automato_state == AUTOMATO_SEQUENCE_RUNNING) {
    automato_end_us = micros();
    automato_stopped = true;
    automato_state = AUTOMATO_SEQUENCE_ABORTED;
  }
  for (index = 0; index < automato_control_count; ++index)
    automato_controls[index].stop();
}

static void automato_advance(void) {
  uint32_t now;
  uint32_t late;
  if (automato_state != AUTOMATO_SEQUENCE_RUNNING)
    return;
  now = micros();
  while (now - automato_entry_start_us >= automato_entries[automato_entry].dwell_us) {
    automato_entry_start_us += automato_entries[automato_entry].dwell_us;
    late = now - automato_entry_start_us;
    if (late > automato_max_late_us)
      automato_max_late_us = late;
    if (++automato_entry >= automato_entry_count) {
      automato_entry = 0;
      if (++automato_repetition >= automato_repeat) {
        automato_end_us = automato_entry_start_us;
        automato_state = AUTOMATO_SEQUENCE_COMPLETED;
        automato_print_sequence("event", "sequence_completed", true);
        return;
      }
    }
    automato_apply(automato_entry);
  }
}

static void automato_handle_sequence(char *arguments) {
  char *word = automato_token(&arguments);
  uint32_t id;
  uint32_t repeat;
  uint32_t dwell;
  if (!word) {
    automato_print_error("invalid_command",
                         "expected SEQ BEGIN, ADD, END, START, or STATUS");
  } else if (strcmp(word, "BEGIN") == 0) {
    if (!automato_parse_u32(automato_token(&arguments), &id) ||
        !automato_parse_u32(automato_token(&arguments), &repeat) ||
        !automato_only_space(arguments)) {
      automato_print_error("invalid_command", "expected SEQ BEGIN seq_id repeat");
    } else if (automato_state == AUTOMATO_SEQUENCE_RUNNING) {
      automato_print_error("busy", "a sequence is running");
    } else if (repeat < 1 || repeat > AUTOMATO_SEQUENCE_MAX_REPEAT) {
      automato_clear();
      automato_print_error("invalid_sequence", "repeat outside 1..max_repeat");
    } else {
      automato_clear();
      automato_sequence_id = id;
      automato_repeat = repeat;
      automato_state = AUTOMATO_SEQUENCE_LOADING;
      automato_print_sequence("sequence", "begun", false);
    }
  } else if (strcmp(word, "ADD") == 0) {
    char *setting;
    struct automato_entry *entry;
    if (automato_state != AUTOMATO_SEQUENCE_LOADING) {
      automato_print_error("sequence_state", "SEQ ADD needs SEQ BEGIN first");
      return;
    }
    if (!automato_parse_u32(automato_token(&arguments), &dwell) || dwell < 1 ||
        dwell > AUTOMATO_SEQUENCE_MAX_DWELL_US) {
      automato_clear();
      automato_print_error("invalid_sequence",
                           "expected SEQ ADD dwell_us [control=value ...]; program cleared");
      return;
    }
    if (automato_entry_count >= AUTOMATO_SEQUENCE_CAPACITY) {
      automato_clear();
      automato_print_error("sequence_full", "entry capacity exceeded; program cleared");
      return;
    }
    entry = &automato_entries[automato_entry_count];
    entry->dwell_us = dwell;
    entry->first_set = automato_setting_count;
    entry->set_count = 0;
    while ((setting = automato_token(&arguments)) != NULL) {
      char *equals = strchr(setting, '=');
      int16_t control;
      uint16_t value;
      if (!equals) {
        automato_clear();
        automato_print_error("invalid_sequence", "expected control=value; program cleared");
        return;
      }
      *equals = '\0';
      control = automato_find(setting);
      if (control < 0 || !automato_parse_value(equals + 1, &value)) {
        automato_clear();
        automato_print_error("invalid_sequence",
                             "unknown control or value outside 0..1; program cleared");
        return;
      }
      if (automato_setting_count >= AUTOMATO_SEQUENCE_SETS) {
        automato_clear();
        automato_print_error("sequence_full", "setting capacity exceeded; program cleared");
        return;
      }
      automato_settings[automato_setting_count].control = (uint8_t)control;
      automato_settings[automato_setting_count].value = value;
      ++automato_setting_count;
      ++entry->set_count;
    }
    ++automato_entry_count;
    /* Compact acknowledgement keeps program loading fast over serial. */
    serial_print("{\"type\":\"sequence\",\"event\":\"added\",\"entries\":");
    serial_print(automato_entry_count);
    serial_print(",\"sets\":");
    serial_print(automato_setting_count);
    serial_println("}");
  } else if (strcmp(word, "END") == 0) {
    if (automato_state != AUTOMATO_SEQUENCE_LOADING || !automato_entry_count) {
      automato_clear();
      automato_print_error("sequence_state", "SEQ END needs a begun program with entries");
    } else {
      automato_state = AUTOMATO_SEQUENCE_LOADED;
      automato_print_sequence("sequence", "loaded", false);
    }
  } else if (strcmp(word, "START") == 0) {
    if (!automato_parse_u32(automato_token(&arguments), &id) ||
        !automato_only_space(arguments)) {
      automato_print_error("invalid_command", "expected SEQ START seq_id");
    } else if (automato_state != AUTOMATO_SEQUENCE_LOADED) {
      automato_print_error(automato_state == AUTOMATO_SEQUENCE_RUNNING ? "busy"
                                                                       : "sequence_state",
                           "SEQ START needs a loaded program");
    } else if (id != automato_sequence_id) {
      automato_print_error("wrong_sequence", "id does not match the loaded program");
    } else {
      automato_entry = 0;
      automato_repetition = 0;
      automato_max_late_us = 0;
      automato_stopped = false;
      automato_end_us = 0;
      automato_start_us = micros();
      automato_entry_start_us = automato_start_us;
      automato_apply(0);
      automato_state = AUTOMATO_SEQUENCE_RUNNING;
      /* Short enough for the 63-byte serial transmit buffer, so printing it
         never blocks loop() while the first entries are due. Ticks and
         state come with the completion line and SEQ STATUS. */
      serial_print("{\"type\":\"sequence\",\"event\":\"started\",\"id\":");
      serial_print(automato_sequence_id);
      serial_println("}");
    }
  } else if (strcmp(word, "STATUS") == 0) {
    automato_print_sequence("sequence", "status", true);
  } else {
    automato_print_error("invalid_command",
                         "expected SEQ BEGIN, ADD, END, START, or STATUS");
  }
}

/* ------------------------------------------------------------ commands */

static void automato_handle(char *line) {
  char *cursor = line;
  char *word = automato_token(&cursor);
  if (!word)
    return;
  if (strcmp(word, "DESCRIBE") == 0) {
    automato_print_descriptor();
  } else if (strcmp(word, "STATUS") == 0) {
    automato_print_status("status");
  } else if (strcmp(word, "STOP") == 0) {
    automato_abort();
    automato_print_status("stopped");
  } else if (strcmp(word, "REST") == 0) {
    uint8_t index;
    if (automato_state == AUTOMATO_SEQUENCE_RUNNING) {
      automato_print_error("busy", "a sequence is running; STOP first");
      return;
    }
    for (index = 0; index < automato_control_count; ++index)
      automato_controls[index].set(automato_controls[index].rest);
    automato_print_status("resting");
  } else if (strcmp(word, "SET") == 0) {
    char *name = automato_token(&cursor);
    char *text = automato_token(&cursor);
    int16_t control = name ? automato_find(name) : -1;
    uint16_t value;
    if (!name || !text || !automato_only_space(cursor)) {
      automato_print_error("invalid_command", "expected SET control value");
    } else if (control < 0) {
      automato_print_error("unknown_control", "control is not advertised");
    } else if (!automato_parse_value(text, &value)) {
      automato_print_error("out_of_range", "value must be a number within 0..1");
    } else if (automato_state == AUTOMATO_SEQUENCE_RUNNING) {
      automato_print_error("busy", "a sequence is running");
    } else {
      automato_controls[control].set(value);
      automato_print_status("accepted");
    }
  } else if (strcmp(word, "SEQ") == 0) {
    automato_handle_sequence(cursor);
  } else {
    automato_print_error("invalid_command",
                         "expected DESCRIBE, STATUS, SET, REST, STOP, or SEQ");
  }
}

static void automato_read(void) {
  while (serial_available()) {
    char value = (char)serial_read();
    if (value == '\r')
      continue;
    if (value == '\n') {
      if (automato_command_overflow) {
        automato_print_error("line_too_long", "command exceeds the line limit");
      } else if (automato_command_length) {
        automato_command[automato_command_length] = '\0';
        automato_handle(automato_command);
      }
      automato_command_length = 0;
      automato_command_overflow = false;
    } else if (!automato_command_overflow) {
      if (automato_command_length < AUTOMATO_COMMAND_LENGTH)
        automato_command[automato_command_length++] = value;
      else
        automato_command_overflow = true;
    }
  }
}

void automato_begin(const char *device, const struct automato_control *controls,
                    uint8_t count) {
  automato_device = device;
  automato_controls = controls;
  automato_control_count = count > AUTOMATO_MAX_CONTROLS ? AUTOMATO_MAX_CONTROLS : count;
  automato_print_descriptor();
}

void automato_announce(void) {
  automato_print_descriptor();
}

void automato_poll(void) {
  uint8_t index;
  for (index = 0; index < automato_control_count; ++index)
    if (automato_controls[index].poll)
      automato_controls[index].poll();
  automato_advance();
  automato_read();
}
