/*
 *  This file is for use by students to define anything they wish.  It is used by both proxy server implementation
 */
 #ifndef __SERVER_STUDENT_H__
 #define __SERVER_STUDENT_H__

 #include "steque.h"

#include <stdbool.h>
#include <assert.h>

/* safe malloc */
void* Malloc(size_t bytes) {
     void *ptr = malloc(bytes);
     assert(ptr != NULL);
     return ptr;
}

/* safe realloc */
void* Realloc(void *ptr, size_t bytes) {
     ptr = realloc(ptr, bytes);
     assert(ptr != NULL);
     return ptr;
}
 
 #endif // __SERVER_STUDENT_H__