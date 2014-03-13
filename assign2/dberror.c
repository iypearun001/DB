#include "dberror.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

char *RC_message;

/* print a message to standard out describing the error */
void 
printError (RC error)
{
  if (RC_message != NULL)
    printf("EC (%i), \"%s\"\n", error, RC_message);
  else
    printf("EC (%i)\n", error);
}

char *
errorMessage (RC error)
{
  char *message;

  if (RC_message != NULL)
    {
      message = (char *) malloc(strlen(RC_message) + 30);
      sprintf(message, "EC (%i), \"%s\"\n", error, RC_message);
    }
  else
    {
      message = (char *) malloc(30);
      sprintf(message, "EC (%i)\n", error);
    }

  return message;
}

static char* errMsgs[]= { 
    "OK", // RC_OK
    "File not found", // RC_FILE_NOT_FOUND
    "File handle not initialized", // RC_FILE_HANDLE_NOT_INIT
    "Write to page file failed", // RC_WRITE_FAILED
    "Trying to read from non existing page", // RC_READ_NON_EXISTING_PAGE
    "Storage manager not initialized", // RC_SM_NOT_INIT

    "Maximum number of open file handles found", // RC_MAX_FILE_HANDLE_OPEN
    "Page file creation failed", // RC_FILE_CREATE_FAILED
    "Page file destroy failed", // RC_FILE_DESTROY_FAILED
    "Page file handle in use", // RC_FILE_HANDLE_IN_USE
    "Page file close failed", // RC_FILE_CLOSE_FAILED
    "Read from page file failed", // RC_READ_FAILED

    "Page frame in use", // RC_FRAME_IN_USE
    "Buffer pool is full", // RC_BUFFER_POOL_FULL
    "Page not pinned", // RC_BUFFER_POOL_FULL
    "Cannot shutdown, page is pinned", // RC_HAVE_PINNED_PAGE
    ""
};

RC set_errormsg(RC error)
{
    RC_message= errMsgs[error];
    return error;
}
