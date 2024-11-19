/*------------------------------------------------------------------------*/
/* Sample code of OS dependent controls for FatFs R0.10b                   */
/* (C)ChaN, 2014                                                          */
/*------------------------------------------------------------------------*/

#include <stdlib.h>		/* ANSI memory controls */
#include "../ff.h"

#if _FS_REENTRANT
/*-----------------------------------------------------------------------
 Create a Synchronization Object
------------------------------------------------------------------------
 This function is called in f_mount function to create a new
 synchronization object, such as semaphore and mutex. When a zero is
 returned, the f_mount function fails with FR_INT_ERR.
*/

int ff_cre_syncobj (	/* TRUE:Function succeeded, FALSE:Could not create due to any error */
	BYTE vol,			/* Corresponding logical drive being processed */
	_SYNC_t *sobj		/* Pointer to return the created sync object */
)
{
  int ret;

  *sobj = xSemaphoreCreateMutex();

  ret = (*sobj != NULL);

  return ret;
}



/*------------------------------------------------------------------------*/
/* Delete a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount function to delete a synchronization
/  object that created with ff_cre_syncobj function. When a zero is
/  returned, the f_mount function fails with FR_INT_ERR.
*/

int ff_del_syncobj (	/* TRUE:Function succeeded, FALSE:Could not delete due to any error */
	_SYNC_t sobj		/* Sync object tied to the logical drive to be deleted */
)
{
  vSemaphoreDelete(sobj);
  return 1;
}


TaskHandle_t pMutexHolder = NULL;

/*------------------------------------------------------------------------*/
/* Request Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on entering file functions to lock the volume.
/  When a zero is returned, the file function fails with FR_TIMEOUT.
*/

int ff_req_grant (	/* TRUE:Got a grant to access the volume, FALSE:Could not get a grant */
	_SYNC_t sobj	/* Sync object to wait */
)
{
  int ret = 1;

  if(xSemaphoreTake(sobj, _FS_TIMEOUT) != pdTRUE)
  {
      // Strangely, the first Take fails often...
    //if(xSemaphoreTake(sobj, _FS_TIMEOUT) != pdTRUE)
    {
      syslog(NULL, "FatFs: %s want mutex but taken by %s\n", pcTaskGetName(NULL), pcTaskGetName(xSemaphoreGetMutexHolder(sobj)));
      ret = 0;
    }
  }
  else
  {
	  //iprintf("FatFs mutex taken by %s\n", pcTaskGetName(NULL));
  }

  return ret;
}



/*------------------------------------------------------------------------*/
/* Release Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on leaving file functions to unlock the volume.
*/

void ff_rel_grant (
	_SYNC_t sobj	/* Sync object to be signaled */
)
{
	  //iprintf("FatFs mutex given by %s\n", pcTaskGetName(NULL));

	  if(xSemaphoreGetMutexHolder(sobj) != xTaskGetCurrentTaskHandle())
		  iprintf("wrong handle...!\n");


  xSemaphoreGive(sobj);
}

#endif




#if _USE_LFN == 3	/* LFN with a working buffer on the heap */
/*------------------------------------------------------------------------*/
/* Allocate a memory block                                                */
/*------------------------------------------------------------------------*/
/* If a NULL is returned, the file function fails with FR_NOT_ENOUGH_CORE.
*/

void* ff_memalloc (	/* Returns pointer to the allocated memory block */
	UINT size		/* Number of bytes to allocate */
)
{
  return pvPortMalloc(size);
}


/*------------------------------------------------------------------------*/
/* Free a memory block                                                    */
/*------------------------------------------------------------------------*/

void ff_memfree(
	void* mblock	/* Pointer to the memory block to free */
)
{
  vPortFree(mblock);
}

#endif
