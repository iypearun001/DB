#include <storage_mgr.h>
//#include <linux/limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_FILE_HANDLE 256 // This can be = max fd's per process
#define BYTES_TO_PAGE(bytes) ((bytes-1) / PAGE_SIZE)
#define PAGE_OFFSET(pageNo)  (pageNo * PAGE_SIZE)

// Management information
typedef struct SM_FileMgmtInfo {
  int fd;
  // we can add some new elements as required, in future.
}SM_FileMgmtInfo;

// Storage manager
typedef struct SM {
   SM_FileHandle* openHandles[MAX_FILE_HANDLE];
   int handleCount;
   int init;
}SM;
static SM storageManager; // As it is static it will be initialized

// STATIC FUNCTIONS
// Is storage manager initialized?
static RC isStorageManagerInitialized()
{
    if (storageManager.init)
        RETURN(RC_OK);
    RETURN(RC_SM_NOT_INIT);
}

// Is fHandle know to Storage Engine ?
static RC isFileHandleOpen(SM_FileHandle *fHandle)
{
    int i;
    int handleCount= storageManager.handleCount;
    for(i=0; i<MAX_FILE_HANDLE && handleCount; i++)
    {
        if (storageManager.openHandles[i] == 0)
            continue;
        handleCount--;
        if (storageManager.openHandles[i] == fHandle)
            RETURN(RC_OK);
    }

    RETURN(RC_FILE_HANDLE_NOT_INIT);
}

// Register the fHandle with Storage Engine
static RC registerFileHandle(SM_FileHandle *fHandle)
{
    int i;
    for(i=0; i<MAX_FILE_HANDLE; i++)
        if (storageManager.openHandles[i] == 0)
        {
            storageManager.openHandles[i]= fHandle;
            storageManager.handleCount++;
            RETURN(RC_OK);
        }

    RETURN(RC_MAX_FILE_HANDLE_OPEN);
}

// De-register the fHandle with Storage Engine
static RC deregisterFileHandle(SM_FileHandle *fHandle)
{
    int i;
    int handleCount= storageManager.handleCount;
    for(i=0; i<MAX_FILE_HANDLE && handleCount; i++)
    {
        if (!storageManager.openHandles[i])
            continue;
        handleCount--;
        if (storageManager.openHandles[i] == fHandle)
        {
            storageManager.openHandles[i]= 0;
            RETURN(RC_OK);
        }
    }

    RETURN(RC_FILE_HANDLE_NOT_INIT);
}

// Get the last page number based on file size.
// We can alternatively store last page number within
// the page file, but it is not necessary for now.
static int getLastPageNo(char* fileName)
{
    struct stat st;
    stat(fileName, &st);
    return BYTES_TO_PAGE(st.st_size); 
}

/************************************************************
 *                    interface                             *
 ************************************************************/
/* manipulating page files */
void initStorageManager (void)
{
    if (isStorageManagerInitialized() != RC_OK)
        storageManager.init= 1;
}

/* Create page file */
RC createPageFile (char *fileName)
{
    int fd;

    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Create file if not exists
    if ((fd= open(fileName, O_CREAT|O_EXCL|O_RDWR, S_IRWXU)) > 0 )
    //if ((fd= open(fileName, O_CREAT|O_RDWR, S_IRWXU)) > 0 )
    {
        // Add 1 page with zerobytes of page size
        char zeropage[PAGE_SIZE];
        memset(zeropage, 0, PAGE_SIZE);
        if (write(fd, zeropage, PAGE_SIZE) < PAGE_SIZE)
        {
          close(fd);
          RETURN(RC_WRITE_FAILED);
        }

        RETURN(RC_OK);
    }
    RETURN(RC_FILE_CREATE_FAILED);
}

/* Open the page file and register it with Storage Engine */
RC openPageFile (char *fileName, SM_FileHandle *fHandle)
{
    int fd;

    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) == RC_OK)
        RETURN(RC_FILE_HANDLE_IN_USE);

    if ((fd= open(fileName, O_RDWR, S_IRWXU)) > 0)
    {
        // Initialize the fHandle
        fHandle->fileName= (char*) malloc(strlen(fileName)+1);
        strcpy(fHandle->fileName, fileName);
        fHandle->curPagePos= 0;
        
        fHandle->totalNumPages= getLastPageNo(fHandle->fileName)+1;

        SM_FileMgmtInfo *mgmtInfo= (SM_FileMgmtInfo*) 
                                     malloc(sizeof(SM_FileMgmtInfo));
        mgmtInfo->fd= fd;
        fHandle->mgmtInfo= mgmtInfo;

        // Register the fHandle
        registerFileHandle(fHandle);

        RETURN(RC_OK);
    }
    RETURN(RC_FILE_NOT_FOUND);
}

/* Close the page file and de-register it from Storage Engine */
RC closePageFile (SM_FileHandle *fHandle)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    // Close the file
    if (close(((SM_FileMgmtInfo*)fHandle->mgmtInfo)->fd) < 0 )
        RETURN(RC_FILE_CLOSE_FAILED);

    // Deregister handle from storage manager
    deregisterFileHandle(fHandle);

    // Free mem allocated for fHandle
    free(fHandle->fileName);
    fHandle->fileName= NULL;
    free(fHandle->mgmtInfo);
    fHandle->mgmtInfo= NULL;

    RETURN(RC_OK);
}

/* Remove the file from file-system */
RC destroyPageFile (char *fileName)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Remove the file
    if (unlink(fileName) < 0)
        RETURN(RC_FILE_DESTROY_FAILED);

    RETURN(RC_OK);
}

/* Read bytes from file-system. This is not exposed, called by API's */
static RC readBytes(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    int fd;
    // Do we have this page?
    if (pageNum >= fHandle->totalNumPages || pageNum < 0)
        RETURN(RC_READ_NON_EXISTING_PAGE);

    // Read the block
    fd= (int) ((SM_FileMgmtInfo*) fHandle->mgmtInfo)->fd;
    lseek(fd, PAGE_OFFSET(pageNum), SEEK_SET);
    if (read(fd, memPage, PAGE_SIZE) < PAGE_SIZE)
        RETURN(RC_READ_FAILED);

    fHandle->curPagePos= pageNum;
    RETURN(RC_OK);
}

/* Write bytes to file-system. This is not exposed, called by API's */
static RC writeBytes(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    int fd;
    // Do we have this page?
    if(pageNum < 0)
        RETURN(RC_READ_NON_EXISTING_PAGE);

    // Read the block
    fd= (int) ((SM_FileMgmtInfo*) fHandle->mgmtInfo)->fd;
    lseek(fd, PAGE_OFFSET(pageNum), SEEK_SET);
    if (pageNum >= fHandle->totalNumPages)
        fHandle->totalNumPages= pageNum+1;
    if (write(fd, memPage, PAGE_SIZE) < PAGE_SIZE)
        RETURN(RC_WRITE_FAILED);

    RETURN(RC_OK);
}

/* Reading specific page from disk */
RC readBlock (int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return readBytes(pageNum, fHandle, memPage);
}

/* Read current page position */
int getBlockPos (SM_FileHandle *fHandle)
{
    return (fHandle->curPagePos);
}

/* Reading first page from disk */
RC readFirstBlock (SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return readBytes(0, fHandle, memPage);
}

/* Reading previous page from disk */
RC readPreviousBlock (SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return readBytes(fHandle->curPagePos-1, fHandle, memPage);
}

/* Reading current page from disk */
RC readCurrentBlock (SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return readBytes(fHandle->curPagePos, fHandle, memPage);
}

/* Reading next page from disk */
RC readNextBlock (SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return readBytes(fHandle->curPagePos+1, fHandle, memPage);
}

/* Reading last page from disk */
RC readLastBlock (SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return readBytes(fHandle->totalNumPages-1, fHandle, memPage);
}

/* writing blocks to a specified page number */
RC writeBlock (int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return writeBytes (pageNum, fHandle, memPage);
}

/* writing blocks to current page number */
RC writeCurrentBlock (SM_FileHandle *fHandle, SM_PageHandle memPage)
{
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    return writeBytes (fHandle->curPagePos, fHandle, memPage);
}

/* Append a new block to page file */
RC appendEmptyBlock (SM_FileHandle *fHandle)
{
    char zeropage[PAGE_SIZE];
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    memset(zeropage, 0, PAGE_SIZE);
	return writeBytes (fHandle->totalNumPages, fHandle, (SM_PageHandle) &zeropage);
}

/* Make sure that page file has specified number of pages */
RC ensureCapacity (int numberOfPages, SM_FileHandle *fHandle)
{
    char zeropage[PAGE_SIZE];
    // Is storage manager initialized?
    if (isStorageManagerInitialized() != RC_OK)
        RETURN(RC_SM_NOT_INIT);

    // Is this handle already in use?
    if (isFileHandleOpen(fHandle) != RC_OK)
        RETURN(RC_FILE_HANDLE_NOT_INIT);

    if (numberOfPages > fHandle->totalNumPages)
    {
        memset(zeropage, 0, PAGE_SIZE);
        return writeBytes (numberOfPages-1, fHandle, (SM_PageHandle) &zeropage);
    }
    RETURN(RC_OK);
}

