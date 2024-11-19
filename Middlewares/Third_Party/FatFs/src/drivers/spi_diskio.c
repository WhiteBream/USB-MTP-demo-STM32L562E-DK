/*   __      __ _   _  _  _____  ____   ____  ____  ____   ___   ___  ___
     \ \_/\_/ /| |_| || ||_   _|| ___| | __ \| __ \| ___| / _ \ |   \/   |
      \      / |  _  || |  | |  | __|  | __ <|    /| __| |  _  || |\  /| |
       \_/\_/  |_| |_||_|  |_|  |____| |____/|_|\_\|____||_| |_||_| \/ |_|
*/
/*! \copyright Copyright (c) 2014-2021, White Bream, https://whitebream.nl
*************************************************************************//*!
 \file      spi_diskio.c
 \brief     FatFS driver for SPI disk
 \version   1.0.0.0
 \since     March 4, 2014
 \date      January 22, 2021

 Driver for FatFS to interface with small SPI-flash storage devices.
 Originally based on the sram_diskio.c driver from STMicroelectronics.
****************************************************************************/

#include "whitebream.h" // CCMRAM check
#include "ff_gen_drv.h"

#include "spi.h"
#include "spi_diskio.h"


#ifndef FLS_SPIDEV_NUM
#define FLS_SPIDEV_NUM  3
#endif

#ifndef FLS_SPIDEV
#define FLS_SPIDEV1(X)  hspi ## X
#define FLS_SPIDEV2(X)  &FLS_SPIDEV1(X)
#define FLS_SPIDEV      FLS_SPIDEV2(FLS_SPIDEV_NUM)
#endif


#define COMMAND_WRITE_ENABLE        0x06
#define COMMAND_READ_STATUS         0x05
#define COMMAND_READ_DATA           0x03
#define COMMAND_FAST_READ           0x0B
#define COMMAND_PAGE_PROGRAM        0x02
#define COMMAND_SECTOR_ERASE        0x20
#define COMMAND_BLOCK_ERASE         0xD8
#define COMMAND_CHIP_ERASE          0xC7
#define COMMAND_READ_IDENTIFICATION 0x9F
#define COMMAND_RESET_ENABLE        0x66
#define COMMAND_RESET               0x99

#define WRITEPAGE                   256
#define SECTOR_SIZE                 512
#define BLOCK_SIZE                  4096

//#define CACHE_TIMEOUT               100
#define CACHE_TIMEOUT               0
#define SPI_TIMEOUT                 5000


/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
static uint32_t vFlashSize = 0;


DSTATUS SPIFLASH_initialize (void);
DSTATUS SPIFLASH_status (void);
DRESULT SPIFLASH_read4k (BYTE*, DWORD, UINT);
DRESULT SPIFLASH_read512 (BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
DRESULT SPIFLASH_write4k (const BYTE*, DWORD, UINT);
DRESULT SPIFLASH_write512 (const BYTE*, DWORD, UINT);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
DRESULT SPIFLASH_ioctl (BYTE, void*);
#endif /* _USE_IOCTL == 1 */


Diskio_drvTypeDef  SPIFLASH_Driver =
{
    SPIFLASH_initialize,
    SPIFLASH_status,
    SPIFLASH_read512,
#if _USE_WRITE == 1
    SPIFLASH_write512,
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
    SPIFLASH_ioctl,
#endif /* _USE_IOCTL == 1 */
};

#if (CACHE_TIMEOUT != 0)
static TimerHandle_t vCacheFlush = NULL;
#endif

#if _USE_WRITE == 1
static uint8_t vBlockCache[BLOCK_SIZE];
static uint32_t vLastBlock = UINT32_MAX / (BLOCK_SIZE / SECTOR_SIZE);
static volatile uint32_t vDirty = 0;

static DRESULT SPIFLASH_cache(uint32_t sector);
#endif


#if _USE_WRITE == 1
#if (CACHE_TIMEOUT != 0)
static void
SPIFLASH_cache_flush(TimerHandle_t pxTimer)
{
    (void)pxTimer;

    //iprintf("SPIFLASH_cache_flush sector=%u\n", vLastBlock << 3);
    SPIFLASH_cache(UINT32_MAX);
}
#endif
#endif


#if _USE_WRITE == 1
static DRESULT
SPIFLASH_cache(uint32_t sector)
{
	DRESULT err = RES_OK;

#if (CACHE_TIMEOUT != 0)
    xTimerStop(vCacheFlush, 0);
#endif

    sector /= BLOCK_SIZE / SECTOR_SIZE;
    if (sector != vLastBlock)
    {
        if ((vLastBlock != UINT32_MAX / (BLOCK_SIZE / SECTOR_SIZE)) && (vDirty != 0))
        {
            //iprintf("LST_4k_Cache write dirty=%02X sector %u\n", vDirty, vLastBlock);
            SPIFLASH_ioctl(SECTOR_ERASE, &vLastBlock);
            err = SPIFLASH_write4k(vBlockCache, vLastBlock, 1);
            vDirty = 0;
        }
        if (sector != UINT32_MAX / (BLOCK_SIZE / SECTOR_SIZE))
        {
            SPIFLASH_read4k(vBlockCache, sector, 1);
            vLastBlock = sector;
        }
    }
    return(err);
}
#endif


#if _USE_WRITE == 1
static void
SPIFLASH_WaitForWriteToFinish(void)
{
    uint8_t status;
    uint32_t timeout = 0;

    HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
    HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t []){COMMAND_READ_STATUS}, 1, SPI_TIMEOUT);

    do
    {
        HAL_SPI_Receive(FLS_SPIDEV, &status, 1, SPI_TIMEOUT);
        timeout++;
    }
    while ((status & 0x01) && (timeout < 100000));

    HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

    if (timeout >= 100000)
    {
        syslog(NULL, "SPIFLASH_WaitForWriteToFinish timeout\n");
    }
}
#endif


/**
  * @brief  Initializes a Drive
  * @param  None
  * @retval DSTATUS: Operation status
  */
DSTATUS
SPIFLASH_initialize(void)
{
    char* pManufacturer = NULL;
    uint8_t rd[10];
    uint8_t manufacturer;
    uint8_t memoryType;
    uint8_t capacity;

#if _USE_WRITE == 1
#if (CACHE_TIMEOUT != 0)
    if (vCacheFlush == NULL)
        vCacheFlush = xTimerCreate("spiCache", CACHE_TIMEOUT / portTICK_PERIOD_MS , pdTRUE, NULL, SPIFLASH_cache_flush);
#endif
#endif

    /* Configure the SPI Flash device device */
    if (HAL_SPI_Mutex(FLS_SPIDEV, true))
    {
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
        HAL_SPI_TransmitReceive(FLS_SPIDEV, (uint8_t []){COMMAND_READ_IDENTIFICATION,0,0,0}, rd, 4, SPI_TIMEOUT);
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

        manufacturer = rd[1];
        memoryType = rd[2];
        capacity = rd[3];

        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
        HAL_SPI_TransmitReceive(FLS_SPIDEV, (uint8_t []){COMMAND_READ_STATUS,0}, rd, 2, SPI_TIMEOUT);
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
        if (rd[1] & 0x01)
        {
            syslog(NULL, "SPIFLASH_initialize reset flash device...");
            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
            HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t []){COMMAND_RESET_ENABLE}, 1, SPI_TIMEOUT);
            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
            HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t []){COMMAND_RESET}, 1, SPI_TIMEOUT);
            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

            vTaskDelay(1);
        }

        switch (manufacturer)
        {
            case 0x01:  pManufacturer = "Spansion"; break;
            case 0x0E:  pManufacturer = "Fremont"; break;
            case 0x1F:  pManufacturer = "Adesto"; break;
            case 0x20:  pManufacturer = "Micron"; break;
            case 0x9D:  pManufacturer = "ISSI"; break;
            case 0xBF:  pManufacturer = "Microchip"; break;
            case 0xC2:  pManufacturer = "Macronix"; break;
            case 0xC8:  pManufacturer = "GigaDevice"; break;
            case 0xEF:  pManufacturer = "Winbond"; break;
            default: break;
        }

        vFlashSize = 0;
        if ((manufacturer == 0x1F) && (capacity == 0x01))   // Adesto
        {
            // Lower 7 bits are memory capacity in megabit
            vFlashSize = 4 * (32 * (memoryType & ~0x80));
        }
        else if ((memoryType == 0x23) || (memoryType == 0x40) || (memoryType == 0x60) || (memoryType == 0x70))
        {
            vFlashSize = 4 << (capacity - 12);
        }
        else if (memoryType == 0x26) // Microchip
        {
            if (capacity == 0x41)
                vFlashSize = 2 * 1024;
        }
        else if (memoryType == 0xBA)
        {
            // 0x19 = 256Mbit => 1 << (19 - 4) = 32K(*1024), 0x20 = 512Mbit => 1 << (20 - 4) = 64K(*1024)
            capacity = ((capacity & 0xF0) >> 4) * 10 + (capacity & 0x0F);
            vFlashSize = 1 << (capacity - 4);
        }

        if (vFlashSize == 0)
            syslog(NULL, "SPIFLASH_initialize found unknown device (ID 0x%X,0x%X,0x%X)", manufacturer, memoryType, capacity);
        else if (vFlashSize < 1024)
            syslog(NULL, "SPIFLASH_initialize found %s %ukB device", pManufacturer, vFlashSize);
        else
            syslog(NULL, "SPIFLASH_initialize found %s %uMB device", pManufacturer, vFlashSize / 1024);

        // Size was determined in kB, save global variable in Bytes
        vFlashSize *= 1024;

        HAL_SPI_Mutex(FLS_SPIDEV, false);
    }
    return(vFlashSize != 0 ? RES_OK : RES_NOTRDY);
}


/**
  * @brief  Gets Disk Status
  * @param  None
  * @retval DSTATUS: Operation status
  */
DSTATUS
SPIFLASH_status(void)
{
    return(RES_OK);
}


/**
  * @brief  Reads Sector(s)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT
SPIFLASH_read4k(BYTE *buff, DWORD block, UINT count)
{
    DRESULT err = RES_OK;
    HAL_StatusTypeDef hal = HAL_OK;
    uint8_t wr[10];

    block *= BLOCK_SIZE;    // Change to address
    count *= BLOCK_SIZE;    // Change to number of bytes

    if (HAL_SPI_Mutex(FLS_SPIDEV, true))
    {
        wr[0] = COMMAND_FAST_READ;
        wr[1] = (block >> 16) & 0xFF;
        wr[2] = (block >> 8) & 0xFF;
        wr[3] = block & 0xFF;
        wr[4] = 0x00;

        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
        HAL_SPI_Transmit(FLS_SPIDEV, wr, 5, SPI_TIMEOUT);

        if (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL))
            hal = HAL_SPI_Receive_IT(FLS_SPIDEV, buff, count);
        else
            hal = HAL_SPI_Receive_DMA(FLS_SPIDEV, buff, count);

        if (hal != HAL_OK)
        {
            syslog(NULL, "SPIFLASH_read4k error %d at addr %p (%s)", hal, (void*)block, (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL)) ? "IT" : "DMA");
            err = RES_NOTRDY;
        }
        else if (HAL_SPI_Wait(FLS_SPIDEV, SPI_TIMEOUT) != pdTRUE)
        {
            syslog(NULL, "SPIFLASH_read4k timeout addr %p (%s)", (void*)block, (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL)) ? "IT" : "DMA");
            err = RES_NOTRDY;
        }
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
        HAL_SPI_Mutex(FLS_SPIDEV, false);
    }
    return(err);
}


DRESULT
SPIFLASH_read512(BYTE *buff, DWORD sector, UINT count)
{
    DRESULT err = RES_OK;
    HAL_StatusTypeDef hal = HAL_OK;

    uint8_t wr[10];

    if (HAL_SPI_Mutex(FLS_SPIDEV, true))
    {
        do
        {
        #if _USE_WRITE == 1
            if ((sector / (BLOCK_SIZE / SECTOR_SIZE)) == vLastBlock)
            {
                uint32_t subsect = sector % (BLOCK_SIZE / SECTOR_SIZE);

                err = SPIFLASH_cache(sector);
                memcpy(buff, &vBlockCache[SECTOR_SIZE * subsect], SECTOR_SIZE);
            }
            else
        #endif
            {
                uint32_t address = sector * SECTOR_SIZE;

                wr[0] = COMMAND_FAST_READ;
                wr[1] = (address >> 16) & 0xFF;
                wr[2] = (address >> 8) & 0xFF;
                wr[3] = address & 0xFF;
                wr[4] = 0x00;

                HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
                HAL_SPI_Transmit(FLS_SPIDEV, wr, 5, SPI_TIMEOUT);

                if (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL))
                    hal = HAL_SPI_Receive_IT(FLS_SPIDEV, buff, SECTOR_SIZE);
                else
                    hal = HAL_SPI_Receive_DMA(FLS_SPIDEV, buff, SECTOR_SIZE);

                if (hal != HAL_OK)
                {
                    syslog(NULL, "SPIFLASH_read512 error %d at addr %p (%s)", hal, (void*)address, (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL)) ? "IT" : "DMA");
                    err = RES_NOTRDY;
                }
                else if (HAL_SPI_Wait(FLS_SPIDEV, SPI_TIMEOUT) != pdTRUE)
                {
                    syslog(NULL, "SPIFLASH_read512 timeout addr %p (%s)", (void*)address, (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL)) ? "IT" : "DMA");
                    err = RES_NOTRDY;
                }
                HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
            }
            sector++;
            buff += SECTOR_SIZE;
        }
        while (--count);
        HAL_SPI_Mutex(FLS_SPIDEV, false);
    }
    return(err);
}


/**
  * @brief  Writes Sector(s)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT SPIFLASH_write512(const BYTE *buff, DWORD sector, UINT count)
{
    DRESULT err = RES_PARERR;
    //HAL_StatusTypeDef hal = HAL_OK;

    if (HAL_SPI_Mutex(FLS_SPIDEV, true))
    {
        while (count--)
        {
            uint32_t subsect = sector % (BLOCK_SIZE / SECTOR_SIZE);

            err = SPIFLASH_cache(sector);
            if (err != RES_OK)
                return err;

            if (memcmp(&vBlockCache[SECTOR_SIZE * subsect], buff, SECTOR_SIZE) != 0)
            {
                vDirty |= 1 << subsect;
                memcpy(&vBlockCache[SECTOR_SIZE * subsect], buff, SECTOR_SIZE);
            }
            buff += SECTOR_SIZE;
            sector++;
        }

        if (vDirty != 0)
        {
    #if(CACHE_TIMEOUT != 0)
            xTimerStart(vCacheFlush, 5);
    #else
            err = SPIFLASH_cache(UINT32_MAX);
    #endif
        }
        HAL_SPI_Mutex(FLS_SPIDEV, false);
    }
    return(err);
}


DRESULT
SPIFLASH_write4k(const BYTE *buff, DWORD block, UINT count)
{
    DRESULT err = RES_OK;
    HAL_StatusTypeDef hal = HAL_OK;
    uint8_t wr[10];
    uint32_t address = block * BLOCK_SIZE;
    uint32_t pageCnt = count * BLOCK_SIZE / WRITEPAGE;
    const uint8_t* dataForVerify = (const uint8_t *)buff;
    uint32_t addressForVerify = address;

    static uint8_t buf[WRITEPAGE];

    assert_param(count == 1);

    if (HAL_SPI_Mutex(FLS_SPIDEV, true))
    {
    #if 0
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
        HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t[]){COMMAND_WRITE_ENABLE}, 1, SPI_TIMEOUT);
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
        vTaskDelay(1);

        wr[0] = COMMAND_SECTOR_ERASE;
        wr[1] = (address >> 16) & 0xFF;
        wr[2] = (address >> 8) & 0xFF;
        wr[3] = address & 0xFF;

        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
        HAL_SPI_Transmit(FLS_SPIDEV, wr, 4, SPI_TIMEOUT);
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

        SPIFLASH_WaitForWriteToFinish();
    #endif

        do
        {
            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
            HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t[]){COMMAND_WRITE_ENABLE}, 1, SPI_TIMEOUT);
            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
            vTaskDelay(1);

            wr[0] = COMMAND_PAGE_PROGRAM;
            wr[1] = (address >> 16) & 0xFF;
            wr[2] = (address >> 8) & 0xFF;
            wr[3] = address & 0xFF;

            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
            HAL_SPI_Transmit(FLS_SPIDEV, wr, 4, SPI_TIMEOUT);

            if (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmatx == NULL))
                hal = HAL_SPI_Transmit_IT(FLS_SPIDEV, (uint8_t*)buff, WRITEPAGE);
            else
                hal = HAL_SPI_Transmit_DMA(FLS_SPIDEV, (uint8_t*)buff, WRITEPAGE);

            if (hal != HAL_OK)
            {
                syslog(NULL, "SPIFLASH_write4k error %d at addr %p (%s)", hal, (void*)address, (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmatx == NULL)) ? "IT" : "DMA");
                err = RES_NOTRDY;
            }
            else if (HAL_SPI_Wait(FLS_SPIDEV, SPI_TIMEOUT) != pdTRUE)
            {
                if (err != RES_NOTRDY)
                    syslog(NULL, "SPIFLASH_write4k program timeout addr %p!", (void*)address);
                err = RES_NOTRDY;
            }
            HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

            SPIFLASH_WaitForWriteToFinish();

            buff += WRITEPAGE;
            address += WRITEPAGE;
        }
        while (--pageCnt);

        // Verify

        wr[0] = COMMAND_FAST_READ;
        wr[1] = (addressForVerify >> 16) & 0xFF;
        wr[2] = (addressForVerify >> 8) & 0xFF;
        wr[3] = addressForVerify & 0xFF;
        wr[4] = 0x00;

        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
        HAL_SPI_Transmit(FLS_SPIDEV, wr, 5, SPI_TIMEOUT);

        for (pageCnt = count * BLOCK_SIZE / sizeof(buf); pageCnt != 0; pageCnt--)
        {
            memset(buf, 0x55, sizeof(buf));
            if (!IS_DMAMEM(buf) || ((FLS_SPIDEV)->hdmarx == NULL))
                hal = HAL_SPI_Receive_IT(FLS_SPIDEV, buf, sizeof(buf));
            else
                hal = HAL_SPI_Receive_DMA(FLS_SPIDEV, buf, sizeof(buf));

            if (hal != HAL_OK)
            {
                syslog(NULL, "SPIFLASH_write4k error %d at addr %p (%s)", hal, (void*)address, (!IS_DMAMEM(buff) || ((FLS_SPIDEV)->hdmarx == NULL)) ? "IT" : "DMA");
                err = RES_NOTRDY;
            }
            else if (HAL_SPI_Wait(FLS_SPIDEV, SPI_TIMEOUT) != pdTRUE)
            {
                if (err != RES_NOTRDY)
                    syslog(NULL, "SPIFLASH_write4k verify timeout addr %p!", (void*)addressForVerify);
                err = RES_NOTRDY;
                pageCnt = 1;
            }
            else if (memcmp(dataForVerify, buf, sizeof(buf)) != 0)
            {
                if(err != RES_NOTRDY)
                    syslog(NULL, "SPIFLASH_write4k verify error addr %p!", (void*)addressForVerify);
                err = RES_ERROR;
                pageCnt = 1;
            }
            dataForVerify += sizeof(buf);
            addressForVerify += sizeof(buf);
        }
        HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
        HAL_SPI_Mutex(FLS_SPIDEV, false);
    }
    return(err);
}
#endif /* _USE_WRITE == 1 */


/**
  * @brief  I/O control operation
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT
SPIFLASH_ioctl(BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;

    if (HAL_SPI_Mutex(FLS_SPIDEV, true))
    {
        switch (cmd)
        {
            /* Make sure that no pending write process */
            case CTRL_SYNC :
                res = SPIFLASH_cache(UINT32_MAX);
                break;

            case SECTOR_ERASE:
                {
                    DWORD val = *(DWORD*)buff * BLOCK_SIZE;
                    uint8_t wr[10];

                    HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
                    HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t[]){COMMAND_WRITE_ENABLE}, 1, SPI_TIMEOUT);
                    HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
                    vTaskDelay(1);

                    wr[0] = COMMAND_SECTOR_ERASE;
                    wr[1] = (val >> 16) & 0xFF;
                    wr[2] = (val >> 8) & 0xFF;
                    wr[3] = val & 0xFF;

                    HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
                    HAL_SPI_Transmit(FLS_SPIDEV, wr, 4, SPI_TIMEOUT);
                    HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

                    SPIFLASH_WaitForWriteToFinish();
                }
                break;

            case DISK_ERASE:
                HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
                HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t[]){COMMAND_WRITE_ENABLE}, 1, 1000);
                HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);
                vTaskDelay(1);

                HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_RESET);
                HAL_SPI_Transmit(FLS_SPIDEV, (uint8_t[]){COMMAND_CHIP_ERASE}, 1, 1000);
                HAL_GPIO_WritePin(FLS_SS, GPIO_PIN_SET);

                vTaskDelay(25000);
                SPIFLASH_WaitForWriteToFinish();
                break;

            /* Get number of sectors on the disk (DWORD) */
            case GET_SECTOR_COUNT :
                *(DWORD*)buff = vFlashSize / SECTOR_SIZE;
                res = RES_OK;
                break;

            /* Get R/W sector size (WORD) */
            case GET_SECTOR_SIZE :
                *(WORD*)buff = SECTOR_SIZE;
                res = RES_OK;
                break;

            /* Get erase block size in unit of sector (DWORD) */
            case GET_BLOCK_SIZE :
                *(DWORD*)buff = BLOCK_SIZE;
                break;

            default:
                res = RES_PARERR;
        }
        HAL_SPI_Mutex(FLS_SPIDEV, false);
    }
    return(res);
}
#endif /* _USE_IOCTL == 1 */
