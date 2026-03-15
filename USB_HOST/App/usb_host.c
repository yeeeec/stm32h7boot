/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file            : usb_host.c
  * @version         : v1.0_Cube
  * @brief           : This file implements the USB Host
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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

#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_msc.h"

/* USER CODE BEGIN Includes */
#include <string.h>

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */
static uint16_t g_usb_vid;
static uint16_t g_usb_pid;
static char g_usb_serial[64];

static void USB_HOST_ClearIdentity(void)
{
  g_usb_vid = 0U;
  g_usb_pid = 0U;
  memset(g_usb_serial, 0, sizeof(g_usb_serial));
}

static void USB_HOST_TrimRight(char *text)
{
  size_t length;

  if (text == NULL)
  {
    return;
  }

  length = strlen(text);
  while (length > 0U)
  {
    const char ch = text[length - 1U];
    if ((ch != ' ') && (ch != '\r') && (ch != '\n') && (ch != '\t'))
    {
      break;
    }
    text[length - 1U] = '\0';
    --length;
  }
}

static void USB_HOST_UpdateIdentity(USBH_HandleTypeDef *phost)
{
  if (phost == NULL)
  {
    return;
  }

  g_usb_vid = phost->device.DevDesc.idVendor;
  g_usb_pid = phost->device.DevDesc.idProduct;

  if (phost->device.DevDesc.iSerialNumber == 0U)
  {
    g_usb_serial[0] = '\0';
    return;
  }

  (void)strncpy(g_usb_serial, (const char *)(void *)phost->device.Data, sizeof(g_usb_serial) - 1U);
  g_usb_serial[sizeof(g_usb_serial) - 1U] = '\0';
  USB_HOST_TrimRight(g_usb_serial);
}

/* USER CODE END 0 */

/*
 * user callback declaration
 */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB host library, add supported class and start the library
  * @retval None
  */
void MX_USB_HOST_Init(void)
{
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */
  USB_HOST_ClearIdentity();

  /* USER CODE END USB_HOST_Init_PreTreatment */

  /* Init host Library, add supported class and start the library. */
  if (USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_RegisterClass(&hUsbHostHS, USBH_MSC_CLASS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_Start(&hUsbHostHS) != USBH_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_HOST_Init_PostTreatment */

  /* USER CODE END USB_HOST_Init_PostTreatment */
}

/*
 * Background task
 */
void MX_USB_HOST_Process(void)
{
  /* USB Host Background task */
  USBH_Process(&hUsbHostHS);
}

uint16_t USB_HOST_GetVid(void)
{
  return g_usb_vid;
}

uint16_t USB_HOST_GetPid(void)
{
  return g_usb_pid;
}

const char *USB_HOST_GetSerial(void)
{
  return g_usb_serial;
}

USBH_StatusTypeDef USB_HOST_GetLunInfo(uint8_t lun, MSC_LUNTypeDef *info)
{
  if (info == NULL)
  {
    return USBH_FAIL;
  }
  return USBH_MSC_GetLUNInfo(&hUsbHostHS, lun, info);
}
/*
 * user callback definition
 */
static void USBH_UserProcess  (USBH_HandleTypeDef *phost, uint8_t id)
{
  /* USER CODE BEGIN CALL_BACK_1 */
  switch(id)
  {
  case HOST_USER_SELECT_CONFIGURATION:
  break;

  case HOST_USER_DISCONNECTION:
  Appli_state = APPLICATION_DISCONNECT;
  USB_HOST_ClearIdentity();
  break;

  case HOST_USER_CLASS_ACTIVE:
  USB_HOST_UpdateIdentity(phost);
  Appli_state = APPLICATION_READY;
  break;

  case HOST_USER_CONNECTION:
  USB_HOST_ClearIdentity();
  Appli_state = APPLICATION_START;
  break;

  default:
  break;
  }
  /* USER CODE END CALL_BACK_1 */
}

/**
  * @}
  */

/**
  * @}
  */

