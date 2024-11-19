/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @author  MCD Application Team
  * @version V1.2.1
  * @date    20-November-2014
  * @brief   SD Disk I/O driver
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

#include <string.h>
#include "whitebream.h" // CCMRAM check
#include "ff_gen_drv.h"
#include "hwconfig.h"


/* Block Size in Bytes */
#define BLOCK_SIZE              512
#define SD_DEFAULT_BLOCK_SIZE   512
#define SD_TIMEOUT              (30 * 1000 / portTICK_PERIOD_MS)


extern SD_HandleTypeDef hsd;

static DSTATUS Stat = STA_NOINIT;
static SemaphoreHandle_t hSdTransfer = nullptr;

static DWORD scratch[BLOCK_SIZE / sizeof(DWORD)]; // Alignment assured,


static DSTATUS SD_initialize(void);
static DSTATUS SD_status(void);
static DRESULT SD_read(BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
static DRESULT SD_write(const BYTE*, DWORD, UINT);
#endif
#if _USE_IOCTL == 1
static DRESULT SD_ioctl(BYTE, void*);
#endif


Diskio_drvTypeDef SD_Driver =
{
    SD_initialize,
    SD_status,
    SD_read,
#if  _USE_WRITE == 1
    SD_write,
#endif /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
    SD_ioctl,
#endif /* _USE_IOCTL == 1 */
};


static bool
SD_checkstatusTimeout(uint32_t timeout)
{
    uint32_t start = xTaskGetTickCount();

    while (xTaskGetTickCount() - start < timeout)
    {
        if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER)
        {
            return(true);
        }
    }
    return(false);
}


/**
  * @brief  Initializes a Drive
  * @param  None
  * @retval DSTATUS: Operation status
  */
static DSTATUS
SD_initialize(void)
{
    Stat = STA_NOINIT;

    if (hSdTransfer == nullptr)
    {
        hSdTransfer = xSemaphoreCreateBinary();
    }

    if (hSdTransfer != nullptr)
    {
        /* Check if the SD card is plugged in the slot */
        if (!HAL_GPIO_ReadPin(SD_DET))
        {
            HAL_SD_DeInit(&hsd);
            if (HAL_SD_Init(&hsd) == HAL_OK)
            {
                if (HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) == HAL_OK)
                {
                    if (SD_checkstatusTimeout(SD_TIMEOUT))
                    {
                        Stat &= ~STA_NOINIT;
                    }
                }
            }
        }
    }
    return(Stat);
}


/**
  * @brief  Gets Disk Status
  * @param  None
  * @retval DSTATUS: Operation status
  */
static DSTATUS
SD_status(void)
{
	if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER)
	{
		Stat &= ~STA_NOINIT;
	}
	else if (!(Stat & STA_NOINIT))
	{
		if (!SD_checkstatusTimeout(SD_TIMEOUT))
		{
			syslog(nullptr, "Lost SD card...");
			Stat |= STA_NOINIT;
		}
	}
    return(Stat);
}


/**
  * @brief  Reads Sector(s)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
static DRESULT
SD_read(BYTE* buff, DWORD sector, UINT count)
{
    DRESULT res = RES_OK;
if(sector == 122432)
	__NOP();

	if (!SD_checkstatusTimeout(SD_TIMEOUT))
	{
		res = 10;
	}
	else if (((uint32_t)buff % sizeof(uint32_t)) || IS_CCMRAM(buff))
	{
		while (count--)
		{
			if (HAL_SD_ReadBlocks_DMA(&hsd, (uint8_t*)scratch, sector++, 1) == HAL_OK)
			{
				if (xSemaphoreTake(hSdTransfer, SD_TIMEOUT) != pdTRUE)
				{
					res = 11;
				}
				else if (!SD_checkstatusTimeout(SD_TIMEOUT))
				{
					res = 12;
				}
			}
			memcpy(buff, scratch, BLOCK_SIZE);
			buff += BLOCK_SIZE;
		}
	}
	else if (HAL_SD_ReadBlocks_DMA(&hsd, (uint8_t*)buff, sector, count) == HAL_OK)
	{
		if (xSemaphoreTake(hSdTransfer, SD_TIMEOUT) != pdTRUE)
		{
			res = 13;
		}
		else if (!SD_checkstatusTimeout(SD_TIMEOUT))
		{
			res = 14;
		}
	}

    if (res != RES_OK)
    {
        syslog(nullptr, "SD rd %lu ERR %u\n", sector, res);
        if (res >= 10)
            res = RES_ERROR;
    }
    return(res);
}


/**
  * @brief  Writes Sector(s)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
static DRESULT
SD_write(const BYTE* buff, DWORD sector, UINT count)
{
    DRESULT res = RES_OK;

	if (!SD_checkstatusTimeout(SD_TIMEOUT))
	{
		res = 10;
	}
	else if (((uint32_t)buff % sizeof(uint32_t)) || IS_CCMRAM(buff))
	{
		while (count--)
		{
			memcpy(scratch, buff, BLOCK_SIZE);
			if (HAL_SD_WriteBlocks_DMA(&hsd, (uint8_t*)scratch, sector++, 1) == HAL_OK)
			{
				if (xSemaphoreTake(hSdTransfer, SD_TIMEOUT) != pdTRUE)
				{
					res = 11;
				}
				else if (!SD_checkstatusTimeout(SD_TIMEOUT))
				{
					res = 12;
				}
			}
			buff += BLOCK_SIZE;
		}
	}
	else if (HAL_SD_WriteBlocks_DMA(&hsd, (uint8_t*)buff, sector, count) == HAL_OK)
	{
		if (xSemaphoreTake(hSdTransfer, SD_TIMEOUT) != pdTRUE)
		{
			res = 13;
		}
		else if (!SD_checkstatusTimeout(SD_TIMEOUT))
		{
			res = 14;
		}
	}

    if (res != RES_OK)
    {
        syslog(nullptr, "SD wr %lu ERR %u\n", sector, res);
        if (res >= 10)
            res = RES_ERROR;
    }
    return(res);
}
#endif


/**
  * @brief  I/O control operation
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
static DRESULT
SD_ioctl(BYTE cmd, void* buff)
{
    DRESULT res = RES_ERROR;
    HAL_SD_CardInfoTypeDef CardInfo;

    if (Stat & STA_NOINIT)
    {
    	return(RES_NOTRDY);
    }

	switch (cmd)
	{
		/* Make sure that no pending write process */
		case CTRL_SYNC :
			res = RES_OK;
		break;

		/* Get number of sectors on the disk (DWORD) */
		case GET_SECTOR_COUNT :
			HAL_SD_GetCardInfo(&hsd, &CardInfo);
			*(DWORD*)buff = CardInfo.LogBlockNbr;
			res = RES_OK;
		break;

		/* Get R/W sector size (WORD) */
		case GET_SECTOR_SIZE :
			HAL_SD_GetCardInfo(&hsd, &CardInfo);
			*(WORD*)buff = CardInfo.LogBlockSize;
			res = RES_OK;
		break;

		/* Get erase block size in unit of sector (DWORD) */
		case GET_BLOCK_SIZE :
			HAL_SD_GetCardInfo(&hsd, &CardInfo);
			*(DWORD*)buff = CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
		break;

		default:
			res = RES_PARERR;
		break;
	}
    return(res);
}
#endif


void
HAL_SD_AbortCallback(SD_HandleTypeDef* hsd)
{
    __BKPT();
}


void
HAL_SD_TxCpltCallback(SD_HandleTypeDef* hsd)
{
    xSemaphoreGiveFromISR(hSdTransfer, nullptr);
}


void
HAL_SD_RxCpltCallback(SD_HandleTypeDef* hsd)
{
    xSemaphoreGiveFromISR(hSdTransfer, nullptr);
}


void
HAL_SD_ErrorCallback(SD_HandleTypeDef* hsd)
{
    xSemaphoreGiveFromISR(hSdTransfer, nullptr);
}
