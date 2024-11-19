/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2013        */
/*                                                                       */
/*   Portions COPYRIGHT 2014 STMicroelectronics                          */
/*   Portions Copyright (C) 2012, ChaN, all right reserved               */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control module to the FatFs module with a defined API.        */
/*-----------------------------------------------------------------------*/

/**
  ******************************************************************************
  * @file    diskio.c
  * @author  MCD Application Team
  * @version V1.2.1
  * @date    20-November-2014
  * @brief   FatFs low level disk I/O module.
  ******************************************************************************
  * @attention
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
//#include "defines.h"
#include "diskio.h"
#include "ff_gen_drv.h"
//#include "rtc.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
extern Disk_drvTypeDef  disk;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS disk_initialize(BYTE pdrv)
{
  DSTATUS stat = RES_OK;

  if(disk.is_initialized[pdrv] == 0)
  {
    if(disk.drv[pdrv]->disk_initialize() == RES_OK)
    {
        disk.is_initialized[pdrv] = 1;
        stat = RES_OK;
    }
    else
        stat = RES_ERROR;
  }
  return stat;
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS disk_status(BYTE pdrv)
{
  DSTATUS stat;

  stat = disk.drv[pdrv]->disk_status();
  return stat;
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  DRESULT res;

  res = disk.drv[pdrv]->disk_read(buff, sector, count);

#ifdef LED_DISK
  if(res == RES_OK)
    LedBlink(LED_DISK, GREEN, 50);
  else
    LedBlink(LED_DISK, RED, 500);
#endif

  return res;
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{

  DRESULT res;

  res = disk.drv[pdrv]->disk_write(buff, sector, count);

#ifdef LED_DISK
  if(res == RES_OK)
    LedBlink(LED_DISK, YELLOW, 50);
  else
    LedBlink(LED_DISK, RED, 500);
#endif

  return res;
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  DRESULT res;

  res = disk.drv[pdrv]->disk_ioctl(cmd, buff);

  return res;
}
#endif /* _USE_IOCTL == 1 */

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD _get_fattime (void)
{
    uint32_t ret = 0;
#ifdef RTC_H
    struct tm vTm;

    HAL_RTC_GetUsefullTime(&vTm, true);

    ret |= ((vTm.tm_year - 80) & 0x7F) << 25;
    ret |= (vTm.tm_mon + 1) << 21;
    ret |= (vTm.tm_mday) << 16;
    ret |= (vTm.tm_hour) << 11;
    ret |= (vTm.tm_min) << 5;
    ret |= (vTm.tm_sec / 2) << 0;
#endif
    return(ret);
}

