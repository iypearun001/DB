APPROACH:

   NOTE:  Extensions
  
   BufferPool functions are thread safe.  CLOCK alogorithm implemented as 
   extension.

1) When client calls pin page, pageHandle is given a pageFrame in memory.
   Page is pinned by incrementing fixCount.  To do this it is first checked if
   a frame in buffer has already been assigned to this page.

   If a frame is not located this way, a free frame from buffer is given to 
   this page and it is read into memory.  Free frame is located using one of
   three algortihms implemented.  

   If there are not any un-pinned pages the buffer pool returns RC code 
   RC_BUFFER_POOL_FULL.

2) BufferPool creates a structure to hold mgmtData.  mgmtData records the
   number of i/o reads and writes from the time the BufferPool is 
   initialized.  mgmtData maintains a stratData structure that records 
   data needed to implement FIFO LRU and CLOCK replacement strategies.  Finally
   BufferPool maintains a pool array to hold pageframes.  

   pageFrames keep relevant information about pages stored in BufferPool.   
   They store how many times a page is pinned, whether a page is dirty and a 
   reference to pageFrame's location in LRU queue. 

   In case, the LRU strategy is implemented, the buffer pool creates a queue
   to hold references to the pages that are in memory and available for 
   removal.

4) Initializing the BufferPool also initializes the PageTable data structure
   that will locate pageFrames in memory. This is similar to how OS maps 
   virtual pages to physical page frames in RAM.
  
   We use similar concept. We use 4 level of page tables,
   Following is bit map of integer that represents page number.
   | Level1 8bit | Level1 8bit | Level1 8bit | Level1 8bit | 
  
   We initially have 2^8 integers pointed to by pt_head;
   Finding frame with given offset: BM_PageFrame* findPageFrame(PageNumber pn);
   -------------------------------
   1) Read Level1 offset and use it as offset to pt_head.
      Value in pt_head at this offset is ptr to level2 page table.
  
   2) Read Level2 offset and use it as offset to pt_head_level2.
      Value in pt_head_level2 at this offset is ptr to level3 page table.
  
   3) Read Level3 offset and use it as offset to pt_head_level3.
      Value in pt_head_level3 at this offset is ptr to level3 page table.
  
   4) Read Level4 offset and use it as offset to pt_head_level4.
      Value in pt_head_level4 at this offset is ptr to page frame.
  
   There exists only 1 page table of size 2^8 bytes always.
   We allocate page table are required.
   We deallocate page table, when there are no entries in use.
   Only level4 page table entry points to 'page frame', rest
   all level<4 page table entries point to next level page table.

5) Three page replacement stratgies are implemented.  

   FIFO records when a page is read into memory. It then keeps track of this
   information and  when a page is being removed it attempts to remove the 
   first added page. If the page is pinned it tries to remove the next most 
   recently added page.

   CLOCK uses the FIFO structure but adds one additional boolean marker to the 
   pageFrame.  This marker indicates whether or not the page was just added
   before it is a candidate for removal.  So when a page is pinned this flag is
   set to false to indicate that this page is not yet a candidate for removal.  
   When the CLOCK algorithm searches for a page for removal, it follows the 
   FIFO approach but if the flag indicates that this page is not a candidate 
   for removal, it adjusts the flag to true. It then continues through all other
   pages, if it returns to this page, then it would now be removed as long as
   it has not been re-pinned in the interim.
   
   To account for the possibility of all pages having removal flag set to false 
   when algortithm is called, the CLOCK algorithm cycles through the buffer
   pool twice before returing with bufferPool full message.  Additionally our
   implementation of CLOCK does not reset the flag for removal to FALSE when 
   marking a page dirty, only when pinning a page. 

   LRU maintains a queue implemented as a linked list to remove pages.  When a 
   page is unpinned, it is checked if there are no clients now holding it (fix
   count = 0).  If this is the case it is added to the end of the queue.  If a 
   page is re-pinned after fix count reaches 0 but before it has been removed 
   from queue, then it needs to be removed from queue as it is no longer the 
   least recently used page.  reuseLRUNode is called to update the queue.  When
   a pageFrame is needed for a new pageHandle the head of the queue is popped.

6) Compilation.  Type "make" to compile code.  A test_assign2 binary is created.  
   Type "make test" at prompt to run test.
