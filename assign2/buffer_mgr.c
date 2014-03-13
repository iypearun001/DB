#include "buffer_mgr.h"
#include <string.h>
#include "storage_mgr.h"
#include "lru_linked_list.h"
#include "page_table.h"
#include "assert.h"

// Some non-interface static functions
static BM_PageFrame* findFreeFrameFIFO(BM_BufferPool *bm);
static BM_PageFrame* findFreeFrameLRU(BM_BufferPool *bm);
static BM_PageFrame* findFreeFrameCLOCK(BM_BufferPool *bm);
static BM_PageFrame* findFreeFrame(BM_BufferPool *bm);
static RC writeIfDirty(BM_BufferPool *const bm, BM_PageFrame *pf);

// Handy lock macros to make BM thread safe.
#define BM_LOCK()   pthread_mutex_lock(&mgmtData->bm_mutex);
#define BM_UNLOCK() pthread_mutex_unlock(&mgmtData->bm_mutex);


// Buffer Manager Interface Pool Handling
// ***************************************
RC initBufferPool(BM_BufferPool *const bm, const char *const pageFileName,
		  const int numPages, ReplacementStrategy strategy,
		  void *stratData)
{
  BM_Pool_MgmtData *mgmtData;
  int i;

  // Initialize Pool
  bm->pageFile= strdup(pageFileName);
  bm->numPages= numPages;
  bm->strategy= strategy;

  // Initialize Pool Mgmt Data
  mgmtData= MAKE_POOL_MGMTDATA();
  mgmtData->io_reads= 0;
  mgmtData->io_writes= 0;
  mgmtData->stratData.fifoLastFreeFrame= -1;
  mgmtData->stratData.lru_head= NULL;
  mgmtData->stratData.lru_tail= NULL;
  mgmtData->stratData.clockCurrentFrame= -1;
  openPageFile(bm->pageFile, &mgmtData->fh);
  initPageTable(&mgmtData->pt_head);

  // Create Pool pages and initialize them
  mgmtData->pool = MAKE_BUFFER_POOL(numPages);
  for (i=0; i<numPages; i++)
  {
    mgmtData->pool[i].dirty= FALSE;
    mgmtData->pool[i].fixCount= 0;
    mgmtData->pool[i].pn= NO_PAGE;
    
    // Add all frames in LRU list
    // representing free frame to use.
    appendMRUFrame(&mgmtData->stratData, &mgmtData->pool[i]);

    mgmtData->pool[i].clockReplaceFlag= TRUE;
  }
  bm->mgmtData= mgmtData;

  // Initialize thread lock
  pthread_mutex_init(&mgmtData->bm_mutex, NULL);

  RETURN(RC_OK);
}

// Close buffer pool
RC shutdownBufferPool(BM_BufferPool *const bm)
{
  RC rc= RC_OK;
  int frmNo;
  BM_PageFrame *pf;
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;

  // Flush dirty pages
  rc= forceFlushPool(bm);
  if (rc != RC_OK)
    RETURN(rc);

  BM_LOCK();
  // Check if we have pinned pages,
  pf= &mgmtData->pool[0];
  for (frmNo=0; frmNo < bm->numPages; frmNo++)
  {
    if (pf->fixCount)
    {
      BM_UNLOCK();
      RETURN(RC_HAVE_PINNED_PAGE);
    }

    // Also reset page table
    if (pf->pn != NO_PAGE)
      resetPageFrame(&mgmtData->pt_head, pf->pn);
    pf++;
  }
  
  rc= closePageFile(&mgmtData->fh);
  if (rc != RC_OK)
  {
    BM_UNLOCK();
    RETURN(rc);
  }

  cleanLRUlist(&mgmtData->stratData);
  free(mgmtData->pool);
  free(bm->pageFile);
  BM_UNLOCK();
  pthread_mutex_destroy(&mgmtData->bm_mutex);
  free(mgmtData);

  RETURN(RC_OK);
}

// Write page frame data to disk
// with dirty=true and fixCount==0
RC forceFlushPool(BM_BufferPool *const bm)
{
  RC rc= RC_OK;
  BM_Pool_MgmtData *mgmtData;
  int frmNo;
  BM_PageFrame *pf;
  mgmtData= bm->mgmtData;

  BM_LOCK();

  pf= &mgmtData->pool[0];
  for (frmNo=0; frmNo < bm->numPages; frmNo++)
  {
    rc= writeIfDirty(bm, pf);
    if (rc!=RC_OK)
      break;
    pf++;
  }

  BM_UNLOCK();

  RETURN(rc);
}

// Buffer Manager Interface Access Pages
// ***************************************
static RC writeIfDirty(BM_BufferPool *const bm, BM_PageFrame *pf)
{
  RC rc;
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;

  if (pf->dirty && pf->fixCount==0)
  {
    rc= writeBlock(pf->pn, &mgmtData->fh, (SM_PageHandle) &pf->data);
    if (rc!=RC_OK)
      RETURN(rc);
    mgmtData->io_writes++;
    pf->dirty= FALSE;
    resetPageFrame(&mgmtData->pt_head, pf->pn);
  }

  RETURN(RC_OK);
}

// Mark page as dirty
RC markDirty (BM_BufferPool *const bm, BM_PageHandle *const page)
{
  BM_PageFrame *pf;
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  BM_LOCK();

  // Check if we already have a frame assigned to this page
  pf= findPageFrame(&mgmtData->pt_head, page->pageNum);
  if (!pf)
  {
    BM_UNLOCK();
    RETURN(RC_PAGE_NOT_PINNED);
  }

  pf->dirty= TRUE;

  BM_UNLOCK();
  RETURN(RC_OK);
}

// Tell buffer manager that I am done using the page
RC unpinPage (BM_BufferPool *const bm, BM_PageHandle *const page)
{
  BM_PageFrame *pf;
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  BM_LOCK();

  // Check if we already have a frame assigned to this page
  pf= findPageFrame(&mgmtData->pt_head, page->pageNum);
  if (!pf)
  {
    BM_UNLOCK();
    RETURN(RC_PAGE_NOT_PINNED);
  }

  // Mark that page frame is not used by client now.
  pf->fixCount--;

  // Add frame back to the list as MRU frame, 
  // so that this can be used, in next pinPage.
  if(pf->fixCount == 0 && bm->strategy == RS_LRU)
	appendMRUFrame(&mgmtData->stratData, pf);

  BM_UNLOCK();
  RETURN(RC_OK);
}

// Force frame to be writtin to disk, if it is marked as dirty.
RC forcePage (BM_BufferPool *const bm, BM_PageHandle *const page)
{
  RC rc= RC_OK;
  BM_PageFrame *pf;
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  BM_LOCK();

  // Check if we already have a frame assigned to this page
  pf= findPageFrame(&mgmtData->pt_head, page->pageNum);
  if (pf)
    rc= writeIfDirty(bm, pf);

  // We force to write dirty block, even if fixCount>0. Last arg=true.
  BM_UNLOCK();
  RETURN(rc);
}

// Read a page and put it in buffer. Mark frame as used.
RC pinPage (BM_BufferPool *const bm, BM_PageHandle *const page, 
	    const PageNumber pageNum)
{
  RC rc;
  BM_PageFrame *pf;
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  BM_LOCK();

  // Check if we already have a frame assigned to this page
  pf= findPageFrame(&mgmtData->pt_head, pageNum);
  if (pf)
  {
    // If fixCount==0, then remove it from LRU
    // Representing that frame is no more free
    if(pf->fixCount==0 && bm->strategy == RS_LRU)
      reuseLRUFrame(&mgmtData->stratData, pf);

    pf->fixCount++;
    page->pageNum= pageNum;
    page->data= (char*)&pf->data;
    if (bm->strategy == RS_CLOCK)
    {
       pf->clockReplaceFlag = FALSE;
    }
    BM_UNLOCK();
    RETURN(RC_OK);
  }

  // Get free frame from pool
  pf= findFreeFrame(bm);
  if (pf==NULL)
  {
    BM_UNLOCK();
    RETURN(RC_BUFFER_POOL_FULL);
  }

  // Read physical page and keep it in buffer
  if (pageNum >= mgmtData->fh.totalNumPages)
  {
    rc= ensureCapacity(pageNum+1, &mgmtData->fh);
    if (rc!=RC_OK)
    {
      BM_UNLOCK();
      return rc;
    }
  }
  rc= readBlock(pageNum, &mgmtData->fh, &pf->data[0]);
  if (rc!=RC_OK)
  {
    BM_UNLOCK();
    return rc;
  }
  mgmtData->io_reads++;

  // Mark page frame as used
  pf->fixCount++;
  pf->pn= page->pageNum= pageNum;
  page->data= &pf->data[0];

  // Map page number to frame;
  setPageFrame(&mgmtData->pt_head, pageNum, pf);

   //Set the flag for the flag as false, which will prevent any replacement of this frame
   if (bm->strategy == RS_CLOCK)
     pf->clockReplaceFlag = FALSE;

  BM_UNLOCK();
  RETURN(RC_OK);
}

/**************************************************
 * Strategy management functions
 */
static BM_PageFrame* findFreeFrame(BM_BufferPool *bm)
{
  switch (bm->strategy)
  {
      case RS_FIFO:
        return findFreeFrameFIFO(bm);
        
      case RS_CLOCK: 
        return findFreeFrameCLOCK(bm);
      case RS_LRU:
        return findFreeFrameLRU(bm);
        
      case RS_LFU:
      case RS_LRU_K:
      default:
        assert(!"Strategy not implemented\n");
  }
}

/*
 * FIFO free page find strategy
 */
static BM_PageFrame* findFreeFrameFIFO(BM_BufferPool *bm)
{
  RC rc;
  int frmNo, curFrame;
  BM_Pool_MgmtData *mgmtData= mgmtData= bm->mgmtData;

  curFrame= mgmtData->stratData.fifoLastFreeFrame+1;
  for (frmNo=0; frmNo < bm->numPages; frmNo++)
  {
    curFrame= curFrame % bm->numPages;
    BM_PageFrame *pf= &mgmtData->pool[curFrame];
    if (pf->fixCount==0)
    {
        if (pf->dirty)
        {
          rc= writeIfDirty(bm, pf);
          if (rc!=RC_OK)
            return NULL;
        }
        else if (pf->pn != NO_PAGE)
        {
          // Reset Map, as we give this frame to different pn.
          resetPageFrame(&mgmtData->pt_head, pf->pn);
        }

        mgmtData->stratData.fifoLastFreeFrame= curFrame;
        return pf;
    }
    curFrame++;
  }

  return NULL;
}

/*
 * LRU free page find strategy
 */
static BM_PageFrame* findFreeFrameLRU(BM_BufferPool *bm)
{
  RC rc;
  BM_PageFrame *pf;
  BM_Pool_MgmtData *mgmtData= mgmtData= bm->mgmtData;

  pf= retriveLRUFrame(&mgmtData->stratData);
  if (!pf)
    return NULL; // All frames pinned
  
  if (pf->dirty)
  {
     rc= writeIfDirty(bm, pf);
     if (rc!=RC_OK)
        return NULL;
  }
  else if (pf->pn != NO_PAGE)
  {
    // Reset Map, as we give this frame to different pn.
    resetPageFrame(&mgmtData->pt_head, pf->pn);
  }

  return pf;
}

/*
 *  CLOCK free page find strategy
 */
static BM_PageFrame* findFreeFrameCLOCK(BM_BufferPool *bm)
{
  RC rc;
  int frmNo, curFrame;
  BM_Pool_MgmtData *mgmtData= mgmtData= bm->mgmtData;

  curFrame= mgmtData->stratData.clockCurrentFrame+1;
  // cycle through buffer so that we can find an unpinned page
  // that may have its flag set to false
  for (frmNo=0; frmNo < bm->numPages * 2 ; frmNo++)
  {
    curFrame= curFrame % bm->numPages;
    BM_PageFrame *pf= &mgmtData->pool[curFrame];
    if (pf->clockReplaceFlag == TRUE)
    {
      if (pf->fixCount==0)
      {
        if (pf->dirty)
        {
          rc= writeIfDirty(bm, pf);
          if (rc!=RC_OK)
            return NULL;
        }
        else if (pf->pn != NO_PAGE)
        {
          // Reset Map, as we give this frame to different pn.
          resetPageFrame(&mgmtData->pt_head, pf->pn);
        }

        mgmtData->stratData.clockCurrentFrame= curFrame;
        return pf;
      }
    }
    else // Set the flag for the flag as true, which will allow
         // any replacement of this frame in future
      pf->clockReplaceFlag = TRUE;

    curFrame++;
  }

  return NULL;
}


// Statistics Interface
// ***************************************
PageNumber *getFrameContents (BM_BufferPool *const bm)
{
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  BM_PageFrame *pf= &mgmtData->pool[0];
  PageNumber *pn;
  int frmNo;
  BM_LOCK();

  pn= (PageNumber*) malloc(bm->numPages*sizeof(PageNumber));

  for (frmNo=0; frmNo < bm->numPages; frmNo++)
  {
    pn[frmNo]= pf->pn;
    pf++;
  }

  BM_UNLOCK();
  return pn;
}
bool *getDirtyFlags (BM_BufferPool *const bm)
{
  bool *dirty_array= (bool*) malloc(bm->numPages*sizeof(bool));
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  int frmNo;
  BM_PageFrame *pf= &mgmtData->pool[0];
  BM_LOCK();

  for (frmNo=0; frmNo < bm->numPages; frmNo++)
  {
    if (pf->dirty)
      dirty_array[frmNo]= TRUE;
    else
      dirty_array[frmNo]= FALSE;
    pf++;
  }

  BM_UNLOCK();
  return dirty_array;
}
int *getFixCounts (BM_BufferPool *const bm)
{
  int *fixCounts= (int*) malloc(bm->numPages*sizeof(int));
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  int frmNo;
  BM_LOCK();

  BM_PageFrame *pf= &mgmtData->pool[0];
  for (frmNo=0; frmNo < bm->numPages; frmNo++)
  {
    fixCounts[frmNo]= pf->fixCount;
    pf++;
  }

  BM_UNLOCK();
  return fixCounts;
}
int getNumReadIO (BM_BufferPool *const bm)
{
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  return mgmtData->io_reads;
}
int getNumWriteIO (BM_BufferPool *const bm)
{
  BM_Pool_MgmtData *mgmtData= bm->mgmtData;
  return mgmtData->io_writes;
}
