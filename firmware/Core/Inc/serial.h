/* Serial shim over the USB CDC device: the small part of Arduino's Serial
   that the ported ggstepper and Automato bridge code uses (print/println of
   strings, integers and floats with N decimals; available/read/flush).

   Output goes to a ring buffer that serial_poll() hands to the USB CDC
   endpoint in chunks, so printing never waits for the host except when the
   ring is full. Output is discarded while the host has not opened the port
   (DTR low) because there is nobody to read it. serial_poll() must be called
   from the main loop; every blocking wait in this file calls it. */
#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Adjustable constants ---- */
/* Transmit ring, bytes; power of two. Holds the largest bridge line (the
   descriptor) with room to spare. */
#define SERIAL_TX_BUFFER_SIZE 2048U
/* Receive ring, bytes; power of two. */
#define SERIAL_RX_BUFFER_SIZE 1024U
/* Bytes handed to the USB stack per transfer. */
#define SERIAL_TX_CHUNK_SIZE 256U
/* How long a print may wait for room in a full transmit ring before the
   output is dropped (host connected but not reading). */
#define SERIAL_TX_STALL_TIMEOUT_MS 50UL /* TODO(verify): choose with the host tooling */
/* How long serial_flush() waits for the host to take the data. */
#define SERIAL_FLUSH_TIMEOUT_MS 200UL /* TODO(verify) */
/* 1: transmit only while the host asserts DTR (what pyserial and terminal
   programs do on open). 0: transmit as soon as the device is configured. */
#define SERIAL_REQUIRE_DTR 1 /* TODO(verify): hosts that never assert DTR get no output */

void serial_init(void);
void serial_poll(void);
int serial_available(void);
/* Next received byte, or -1. */
int serial_read(void);
/* Waits (bounded) until everything printed has been handed to the host. */
void serial_flush(void);
/* True once for each time the host opens the port. A real Arduino board
   resets when the port is opened and prints its startup line; the bridge
   repeats its descriptor on this event instead. */
bool serial_take_open_event(void);
/* Bytes dropped because the host did not read (diagnostic). */
uint32_t serial_dropped_bytes(void);

void serial_write(const char *data, size_t length);
void serial_print_str(const char *text);
void serial_print_char(char value);
void serial_print_u32(uint32_t value);
void serial_print_i32(int32_t value);
/* Upper-case hexadecimal without prefix or padding, like print(x, HEX). */
void serial_print_hex(uint32_t value);
/* Arduino's algorithm: round at `digits`, "nan", "inf", "ovf" past +-4294967040. */
void serial_print_float(double value, uint8_t digits);
/* "\r\n", like Serial.println(). */
void serial_newline(void);

/* ---- called by the USB CDC glue (USB_Device/App/usbd_cdc_if.c) ---- */
/* Interrupt context. Returns false when the receive ring cannot take another
   full packet, in which case the caller re-arms later via serial_poll(). */
bool serial_usb_received(const uint8_t *data, uint32_t length);
void serial_usb_line_state(bool dtr);

static inline void serial_print_float2(double value) {
  serial_print_float(value, 2);
}

/* print(x): strings, characters and integers; floats get 2 decimals (Arduino
   default). A C character constant has type int, so write "," not ','. */
#define serial_print(value) _Generic((value), \
    char *: serial_print_str, const char *: serial_print_str, \
    char: serial_print_char, \
    _Bool: serial_print_u32, \
    unsigned char: serial_print_u32, unsigned short: serial_print_u32, \
    unsigned int: serial_print_u32, unsigned long: serial_print_u32, \
    signed char: serial_print_i32, short: serial_print_i32, \
    int: serial_print_i32, long: serial_print_i32, \
    float: serial_print_float2, double: serial_print_float2)(value)
#define serial_println(value) (serial_print(value), serial_newline())

#endif
