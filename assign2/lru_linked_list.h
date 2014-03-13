#ifndef LRU 
#define LRU
#include "buffer_mgr.h"

BM_PageFrame* retriveLRUFrame(BM_StrategyInfo *si);
void appendMRUFrame (BM_StrategyInfo *si, BM_PageFrame *pf);
void reuseLRUFrame(BM_StrategyInfo *si, BM_PageFrame *pf);
void cleanLRUlist   (BM_StrategyInfo *si);
#endif
