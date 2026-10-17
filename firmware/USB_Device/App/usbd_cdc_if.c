/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           USB_Device/App/usbd_cdc_if.c
  * @author         MCD Application Team
  * @brief          This file implements the USB device descriptors.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

#include "main.h"
#include "serial.h"

/* USB CDC interface for the serial shim. Derived from ST's CDC_Standalone
   example (STM32G0C1E-EV), with the example's USART bridging removed: received
   bytes go to the serial shim's receive ring and the shim's transmit ring is
   sent from serial_poll() through CDC_Transmit_FS(). */

/** Received data over USB are stored in this buffer (one packet). */
static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

static USBD_CDC_LineCodingTypeDef LineCoding =
{
  115200, /* baud rate*/
  0x00,   /* stop bits-1*/
  0x00,   /* parity - none*/
  0x08    /* nb. of bits 8*/
};

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, NULL, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
}

/**
  * @brief  DeInitializes the CDC media low layer (USB reset, disconnect, config cleared)
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  serial_usb_line_state(false);
  return (USBD_OK);
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters); for
  *               requests without data stage the USBD_SetupReqTypedef
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  UNUSED(length);
  switch (cmd)
  {
  case CDC_SEND_ENCAPSULATED_COMMAND:
  case CDC_GET_ENCAPSULATED_RESPONSE:
  case CDC_SET_COMM_FEATURE:
  case CDC_GET_COMM_FEATURE:
  case CDC_CLEAR_COMM_FEATURE:
    break;

  case CDC_SET_LINE_CODING:
    /* Stored and reported back only: the port has no UART behind it. */
    LineCoding.bitrate    = (uint32_t)(pbuf[0] | (pbuf[1] << 8) |\
                            (pbuf[2] << 16) | ((uint32_t)pbuf[3] << 24));
    LineCoding.format     = pbuf[4];
    LineCoding.paritytype = pbuf[5];
    LineCoding.datatype   = pbuf[6];
    break;

  case CDC_GET_LINE_CODING:
    pbuf[0] = (uint8_t)(LineCoding.bitrate);
    pbuf[1] = (uint8_t)(LineCoding.bitrate >> 8);
    pbuf[2] = (uint8_t)(LineCoding.bitrate >> 16);
    pbuf[3] = (uint8_t)(LineCoding.bitrate >> 24);
    pbuf[4] = LineCoding.format;
    pbuf[5] = LineCoding.paritytype;
    pbuf[6] = LineCoding.datatype;
    break;

  case CDC_SET_CONTROL_LINE_STATE:
    /* wValue bit 0 is DTR: the host has the port open. */
    serial_usb_line_state((((USBD_SetupReqTypedef *)(void *)pbuf)->wValue & 0x0001U) != 0U);
    break;

  case CDC_SEND_BREAK:
  default:
    break;
  }

  return (USBD_OK);
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function. The OUT endpoint NAKs until the next
  *         USBD_CDC_ReceivePacket(), so a full receive ring pushes back on the host.
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  if (serial_usb_received(Buf, *Len))
  {
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  /* Otherwise serial_poll() re-arms the endpoint once the ring has room. */
  return (USBD_OK);
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function. Buf must stay valid until the transfer ends.
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassDataCmsit[0];
  if (hcdc == NULL)
  {
    return USBD_FAIL;
  }
  if (hcdc->TxState != 0)
  {
    return USBD_BUSY;
  }
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}

/**
  * @brief  CDC_TransmitCplt_FS
  *         Data transmitted callback (IN transfer complete)
  */
static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  return (USBD_OK);
}
