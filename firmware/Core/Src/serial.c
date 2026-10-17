#include "serial.h"

#include <math.h>

#include "board_time.h"
#include "main.h"
#include "usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

#if (SERIAL_TX_BUFFER_SIZE & (SERIAL_TX_BUFFER_SIZE - 1U)) || \
    (SERIAL_RX_BUFFER_SIZE & (SERIAL_RX_BUFFER_SIZE - 1U))
#error "serial buffer sizes must be powers of two"
#endif

/* Transmit ring: producer and consumer are both the main loop. */
static uint8_t tx_ring[SERIAL_TX_BUFFER_SIZE];
static uint32_t tx_head = 0; /* next byte to write */
static uint32_t tx_tail = 0; /* next byte to send */
static uint8_t tx_chunk[SERIAL_TX_CHUNK_SIZE]; /* owned by the USB stack while a transfer runs */
static bool tx_stalled = false;
static uint32_t dropped = 0;

/* Receive ring: producer is the USB interrupt, consumer the main loop. */
static uint8_t rx_ring[SERIAL_RX_BUFFER_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;
static volatile bool rx_rearm_needed = false;

static volatile bool host_open = false;
static volatile bool open_edge = false;
static bool open_event = false;

void serial_init(void) {
  tx_head = tx_tail = 0;
  rx_head = rx_tail = 0;
  host_open = false;
}

static uint32_t tx_used(void) { return (tx_head - tx_tail) & (SERIAL_TX_BUFFER_SIZE - 1U); }
static uint32_t rx_used(void) { return (rx_head - rx_tail) & (SERIAL_RX_BUFFER_SIZE - 1U); }

static bool usb_ready(void) {
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
    return false;
#if SERIAL_REQUIRE_DTR
  return host_open;
#else
  return true;
#endif
}

void serial_poll(void) {
  uint32_t count;
  uint32_t index;

  if (open_edge) {
    open_edge = false;
    tx_tail = tx_head; /* anything printed before the host opened the port is stale */
    tx_stalled = false;
    open_event = true;
  }

  if (rx_rearm_needed && SERIAL_RX_BUFFER_SIZE - 1U - rx_used() >= CDC_DATA_FS_MAX_PACKET_SIZE) {
    rx_rearm_needed = false;
    NVIC_DisableIRQ(USB_UCPD1_2_IRQn);
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    NVIC_EnableIRQ(USB_UCPD1_2_IRQn);
  }

  if (!usb_ready()) {
    tx_tail = tx_head; /* nobody to read it */
    return;
  }
  count = tx_used();
  if (!count)
    return;
  if (count > SERIAL_TX_CHUNK_SIZE)
    count = SERIAL_TX_CHUNK_SIZE;
  {
    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassDataCmsit[0];
    if (hcdc == NULL || hcdc->TxState != 0U)
      return; /* previous transfer still running */
  }
  for (index = 0; index < count; ++index)
    tx_chunk[index] = tx_ring[(tx_tail + index) & (SERIAL_TX_BUFFER_SIZE - 1U)];
  if (CDC_Transmit_FS(tx_chunk, (uint16_t)count) == USBD_OK) {
    tx_tail = (tx_tail + count) & (SERIAL_TX_BUFFER_SIZE - 1U);
    tx_stalled = false;
  }
}

int serial_available(void) {
  return (int)rx_used();
}

int serial_read(void) {
  uint8_t value;
  if (rx_head == rx_tail)
    return -1;
  value = rx_ring[rx_tail];
  rx_tail = (rx_tail + 1U) & (SERIAL_RX_BUFFER_SIZE - 1U);
  return value;
}

void serial_flush(void) {
  uint32_t started = micros();
  while (usb_ready() && tx_used()) {
    serial_poll();
    if ((uint32_t)(micros() - started) >= SERIAL_FLUSH_TIMEOUT_MS * 1000UL)
      return;
  }
}

bool serial_take_open_event(void) {
  bool event = open_event;
  open_event = false;
  return event;
}

uint32_t serial_dropped_bytes(void) {
  return dropped;
}

static void put_byte(uint8_t value) {
  uint32_t started;
  if (!usb_ready()) {
    return; /* host not listening: nothing to buffer */
  }
  if (tx_used() == SERIAL_TX_BUFFER_SIZE - 1U) {
    if (tx_stalled) {
      ++dropped;
      return;
    }
    started = micros();
    while (tx_used() == SERIAL_TX_BUFFER_SIZE - 1U) {
      serial_poll();
      if (!usb_ready())
        return;
      if ((uint32_t)(micros() - started) >= SERIAL_TX_STALL_TIMEOUT_MS * 1000UL) {
        tx_stalled = true;
        ++dropped;
        return;
      }
    }
  }
  tx_ring[tx_head] = value;
  tx_head = (tx_head + 1U) & (SERIAL_TX_BUFFER_SIZE - 1U);
}

void serial_write(const char *data, size_t length) {
  while (length--)
    put_byte((uint8_t)*data++);
}

void serial_print_str(const char *text) {
  while (*text)
    put_byte((uint8_t)*text++);
}

void serial_print_char(char value) {
  put_byte((uint8_t)value);
}

void serial_print_u32(uint32_t value) {
  char digits[10];
  uint8_t count = 0;
  do {
    digits[count++] = (char)('0' + value % 10U);
    value /= 10U;
  } while (value);
  while (count)
    put_byte((uint8_t)digits[--count]);
}

void serial_print_i32(int32_t value) {
  if (value < 0) {
    put_byte('-');
    serial_print_u32((uint32_t)(-(int64_t)value));
  } else {
    serial_print_u32((uint32_t)value);
  }
}

void serial_print_hex(uint32_t value) {
  char digits[8];
  uint8_t count = 0;
  do {
    uint8_t nibble = (uint8_t)(value & 0x0FU);
    digits[count++] = (char)(nibble < 10U ? (char)('0' + nibble) : (char)('A' + nibble - 10U));
    value >>= 4;
  } while (value);
  while (count)
    put_byte((uint8_t)digits[--count]);
}

/* Same algorithm as Arduino's Print::printFloat, so the digits match what
   the AVR bridge printed for the same value (apart from AVR's 32-bit double
   precision). */
void serial_print_float(double number, uint8_t digits) {
  double rounding = 0.5;
  double remainder;
  uint32_t integer_part;
  uint8_t index;

  if (isnan(number)) {
    serial_print_str("nan");
    return;
  }
  if (isinf(number)) {
    serial_print_str("inf");
    return;
  }
  if (number > 4294967040.0 || number < -4294967040.0) {
    serial_print_str("ovf");
    return;
  }
  if (number < 0.0) {
    put_byte('-');
    number = -number;
  }
  for (index = 0; index < digits; ++index)
    rounding /= 10.0;
  number += rounding;
  integer_part = (uint32_t)number;
  remainder = number - (double)integer_part;
  serial_print_u32(integer_part);
  if (digits > 0)
    put_byte('.');
  while (digits-- > 0) {
    unsigned int digit;
    remainder *= 10.0;
    digit = (unsigned int)remainder;
    serial_print_u32(digit);
    remainder -= (double)digit;
  }
}

void serial_newline(void) {
  put_byte('\r');
  put_byte('\n');
}

bool serial_usb_received(const uint8_t *data, uint32_t length) {
  uint32_t index;
  for (index = 0; index < length; ++index) {
    uint32_t next = (rx_head + 1U) & (SERIAL_RX_BUFFER_SIZE - 1U);
    if (next == rx_tail)
      break; /* cannot happen while the packet size check below holds */
    rx_ring[rx_head] = data[index];
    rx_head = next;
  }
  if (SERIAL_RX_BUFFER_SIZE - 1U - rx_used() >= CDC_DATA_FS_MAX_PACKET_SIZE)
    return true;
  rx_rearm_needed = true;
  return false;
}

void serial_usb_line_state(bool dtr) {
  if (dtr && !host_open)
    open_edge = true;
  host_open = dtr;
}
