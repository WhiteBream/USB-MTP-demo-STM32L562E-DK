/**
  ******************************************************************************
  * @file    sram_diskio.h
  * @author  MCD Application Team
  * @version V1.2.1
  * @date    20-November-2014
  * @brief   Header for sram_diskio.c module
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SPI_DISKIO_H
#define __SPI_DISKIO_H

/* Includes ------------------------------------------------------------------*/
#include "ff_gen_drv.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
#define SECTOR_ERASE    105
#define DISK_ERASE      106

/* Exported functions ------------------------------------------------------- */
extern Diskio_drvTypeDef  SPIFLASH_Driver;

extern DRESULT SPIFLASH_read4k(BYTE *buff, DWORD sector, UINT count);
extern DRESULT SPIFLASH_write4k(const BYTE *buff, DWORD sector, UINT count);
extern DRESULT SPIFLASH_ioctl(BYTE cmd, void *buff);


#endif /* __SPI_DISKIO_H */

