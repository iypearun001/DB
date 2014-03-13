APPROACH: 

- RM_TableMgmtData for the number of tuples and the the location of the first 
  page with free memory is stored in the first page of the table pageFile.  
  Also stored in the header is the number of table attributes and the keySize.  
  When createTable() is called,  a pointer to char data records the number of 
  tuples, the location of the first free page, the number of table attributes 
  and the keySize. Each of these parameters is recorded as an integer, so the 
  first 16 bytes of the table contains this information.  The next information 
  recorded is the attribute names of the table.  Each attribute is afforded 64 
  bytes of space and is recorded as a /0 delimited string.  This completes the 
  recording of the schema in the pageFile.

  Schema size is limited to one page (4096 bytes).  Record size is similarily 
  constrained. Record size must be no larger then page size minus one byte for 
  TOMBSTONE/freeSpace  marker and page pointers to next and previous pages.

- When openTable() is called, we know the header in page 0 contains the 
  RM_TableMgmtData as well as information describing the table schema.  As the 
  first 16 bytes will contain the number of tuples in the table, the location of 
  the first page with free memory, the number of table attributes, and the 
  keySize.

  After the first 8 bytes are copied into the RM_TableMgmtData struct by using 
  an offset, the schema number of attributes, key size and attribute names can 
  be copied into the struct next.

- closeTable() updates the number of tuples in the table relation then shuts 
  down the buffer pool and frees schema memory.

- insert() adds a new record to an open table.  The first page with free memory
  is pointed to by the RM_TableData struct so this information is  available to 
  this function. If a free page does not exist, appendEmptyBlock() is called to 
  add a page. Then the record page.id is set and slot.id is set.  If a free 
  page is available, then a free slot is accessed by searching through the page 
  and checking a byte located before each record that describes whether the 
  space is free or not.  If a slot is available then the record slot.id is 
  assigned this slot and the record data is copied into the page.  If the free 
  page does not actually have a free slot then appendEmptyBlock will be called 
  and a new page is added.

  This space serves as a TOMBSTONE marker as well.  If space is free byte is 
  set to 0.  If space is not free it is set to 1.  If it represents a deleted 
  record then it is set to -1.

- Adding new pages is done automatically as records are inserted.  Pages are 
  not deleted automatically.  This is a design decision as we assume the pages 
  will fill up again. Each page in the table pageFile is defined by the 
  RM_DataPage struct.  Pointers to previous pages and next pages are maintained 
  for each page.  The remaining section of the page is data.  As records are a 
  fixed size, record->slot.id points to where the record is located.

- Scans are handled by searching through dataPages. Records are scanned in page 
  order. That is records on page one are scanned and then records on page two 
  are scanned. Each slot is examined from slot 0 to final slot (determined by 
  record size).  

  To scan records, startScan is called with the relation, a RM_scanHandle, and 
  an expression that each record is evaluated against.  This field may be NULL.  
  This initializes the RM_ScanMgmtData struct keeps track of number of tuples 
  scanned in scanCount. Thus when startScan is called this is set to 0.  Also 
  the expression for record valuation is set.  The RM_ScanHandle is the client 
  interface for the scan.

  When next is called with the RM_ScanHandle and a record that the client must 
  create, the scan will begin at page 1 and slot 0.  (Recall page 0 stores 
  schema and other pageFile information.  Then the scanCount is incremented.  
  Scanning is handled in a do while loop.  If NULL is passed in as the 
  expression you are testing your tuples against, then the loop exits after one 
  pass. If an expression is used, then the record returned is tested to see if 
  it satisfies the expression.  If it does, the function returns.  If it does 
  not, the loop is executed again to find a tuple that satisfies the 
  expression.

  When the number of tuples scanned is equal to the number of tuples in the 
  table, the next function returns RC_NO_MORE_TUPLES.  This ends the scan.  
  scanClose is then called to free up resources.

- It is possible for parallel scans.  Client may simply open two scans on the 
  same table relation.

- valgrind has been run and any memory leaks have been eliminated.  Running 
  'make valgrindtest' will get results.

  TESTING

- Tests are extended to include tests for trying to create too large of schema 
  and records of too large of size.  

- Tests are written to check for nested expressions evaluating correctly.  
  Tests for inserting 10,000 tuples and then modifying a specific tuple are 
  created and pass.  

- Opening multiple instances of same table and conducting concurrent scan is
  tested.
