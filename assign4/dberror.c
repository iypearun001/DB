#include "dberror.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

char *RC_message;
typedef struct errMsg
{
    int code;
    char *msg;
} errMsg;
static errMsg errMsgs[]= { 
    { RC_OK, "OK"},
    { RC_FILE_NOT_FOUND, "File not found"},
    { RC_FILE_HANDLE_NOT_INIT, "File handle not initialized"},
    { RC_WRITE_FAILED, "Write to page file failed"},
    { RC_READ_NON_EXISTING_PAGE, "Trying to read from non existing page"},
    { RC_SM_NOT_INIT, "Storage manager not initialized"},

    { RC_MAX_FILE_HANDLE_OPEN, "Maximum number of open file handles found"},
    { RC_FILE_CREATE_FAILED, "Page file creation failed"},
    { RC_FILE_DESTROY_FAILED, "Page file destroy failed"},
    { RC_FILE_HANDLE_IN_USE, "Page file handle in use"},
    { RC_FILE_CLOSE_FAILED, "Page file close failed"},
    { RC_READ_FAILED, "Read from page file failed"},

    { RC_FRAME_IN_USE, "Page frame in use"},
    { RC_BUFFER_POOL_FULL, "Buffer pool is full"},
    { RC_PAGE_NOT_PINNED, "Page not pinned"},
    { RC_HAVE_PINNED_PAGE, "Cannot shutdown, page is pinned"},

    { RC_RM_COMPARE_VALUE_OF_DIFFERENT_DATATYPE, "Incompatible types"},
    { RC_RM_EXPR_RESULT_IS_NOT_BOOLEAN, "Result is not a boolean"},
    { RC_RM_BOOLEAN_EXPR_ARG_IS_NOT_BOOLEAN, "Not a boolean expression"},
    { RC_RM_NO_MORE_TUPLES, "No more tuples in relation"},
    { RC_RM_NO_PRINT_FOR_DATATYPE, "No print for datatype"},
    { RC_RM_UNKOWN_DATATYPE, "Unknown datatype"},
    { RC_TOO_LARGE_SCHEMA, "Too large schema for a relation"},
    { RC_TOO_LARGE_RECORD, "Too large record size"},
    { RC_RM_INSERT_FAILED, "Record insert failed"},
    { RC_RM_DELETE_FAILED, "Record deletion failed"},
    { RC_RM_UPDATE_FAILED, "Record update failed"},
    { RC_ORDER_TOO_HIGH_FOR_PAGE, "Order TOO high to fit in a page"},
    { -1, ""}
};

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

RC set_errormsg(RC error)
{
    int i=0;
    while(errMsgs[i].code != -1)
    {
        if (errMsgs[i].code == error)
        {
            RC_message= errMsgs[i].msg;
            break;
        }
        i++;
    }
    return error;
}
