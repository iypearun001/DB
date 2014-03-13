#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H
#include <math.h>
#include "buffer_mgr.h"

// Initialize complete page table to 0
void initPageTable(BM_PageTable *pt);

// Map: Set page with a frame
void setPageFrame(BM_PageTable *pt, PageNumber pn, BM_PageFrame *frame);

// Finding frame with given offset
BM_PageFrame* findPageFrame(BM_PageTable *pt, PageNumber pn);

// Remove mapping page to frame.
void resetPageFrame(BM_PageTable *pt, PageNumber pn);

#endif
