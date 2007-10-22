#include <stdlib.h>
#include "codemuxlib.h"
#include "debug.h"
#include "queue.h"

#ifdef DEBUG_MEMORY_LEAK

/* Maximum length of the location string containing function, file and
   the line number of the memory allocation part in the original
   source code. The location info is added in front of the pointer
   when allocating memory and return p + MAX_LOCATION_STRLEN to the
   user */
#define MAX_LOCATION_STRLEN 512 

typedef struct MemoryAllocInfo {
  char *mi_locstr;             /* location string "__FUNCTION__:__LINE__" */
  int  mi_count;               /* allocated memory counts */
  LIST_ENTRY(MemoryAllocInfo) mi_hash;
  LIST_ENTRY(MemoryAllocInfo) mi_all;			 
} MemoryAllocInfo;

#define MAX_BINS 257 /* a prime number near 256 */
static LIST_HEAD(, MemoryAllocInfo) miBins[MAX_BINS];
static LIST_HEAD(, MemoryAllocInfo) miHead = LIST_HEAD_INITIALIZER(miHead);

/*-------------------------------------------------------------------------*/
void 
dbg_print_memtrace(void)
{
  MemoryAllocInfo *walk;

  TRACE("memory alloc counts dump begins\n");
  LIST_FOREACH(walk, &miHead, mi_all) {
    TRACE("%-50s: %d\n", walk->mi_locstr, walk->mi_count);
  }
  TRACE("memory alloc counts dump ends\n");
}
/*-------------------------------------------------------------------------*/
static void 
increase_alloc_count(char* p, const char* func, 
		     const char* file, const int line)
{
  int bin;
  MemoryAllocInfo* walk;

#define MAX_LINE_DIGIT 7 /* million lines of code per file is way too much */
#define LOCSTR_LENGH  (strlen(func) + strlen(file) + MAX_LINE_DIGIT + 3)

  if (LOCSTR_LENGH >= MAX_LOCATION_STRLEN) {
    TRACE("over the length limit %s:%d\n", func, line);
    FlushLogF(hdebugLog);
    exit(-1);
  }
  snprintf(p, MAX_LOCATION_STRLEN, "%s:%s:%d", func, file, line);

  bin = (int)(HashString(p, 0, FALSE, FALSE) % MAX_BINS);
  LIST_FOREACH(walk, &miBins[bin], mi_hash) {
    if (strcmp(walk->mi_locstr, p) == 0) { /* match */
      walk->mi_count++;
      return;
    }
  }
  
  /* allocate it if not found */
  if ((walk = (MemoryAllocInfo *)calloc(1, sizeof(MemoryAllocInfo))) == NULL) {
    TRACE("calloc failed\n");
    FlushLogF(hdebugLog);
    exit(-1);
  }
  if ((walk->mi_locstr = strdup(p)) == NULL) {
    TRACE("calloc failed\n");
    FlushLogF(hdebugLog);
    exit(-1);
  }
  walk->mi_count++;
  LIST_INSERT_HEAD(&miBins[bin], walk, mi_hash);
  LIST_INSERT_HEAD(&miHead, walk, mi_all);
}
/*-------------------------------------------------------------------------*/
static void
decrease_alloc_count(char *p, const char* func, 
		     const char* file, const int line)
{ 
  int bin = (int)(HashString(p, 0, FALSE, FALSE) % MAX_BINS);
  MemoryAllocInfo* walk;

  LIST_FOREACH(walk, &miBins[bin], mi_hash) {
    if (strcmp(walk->mi_locstr, p) == 0) { /* match */
      walk->mi_count--;
      if (walk->mi_count == 0) {
	LIST_REMOVE(walk, mi_hash);
	LIST_REMOVE(walk, mi_all);
	free(walk->mi_locstr);
	free(walk);
      }
      return;
    }
  }

  TRACE("decrease failed %s:%s:%d\n", func, file, line);
  FlushLogF(hdebugLog);
  exit(-1);
}
/*-------------------------------------------------------------------------*/
void *
dbgcalloc(size_t nmemb, size_t size, 
	  const char* func,  const char* file, const int line)
{
  int msize = nmemb * size + MAX_LOCATION_STRLEN;
  char *p;

  if ((p = (char *)calloc(1, msize)) != NULL) {
    increase_alloc_count(p, func, file, line);
    return (void *)(p + MAX_LOCATION_STRLEN);
  }
  return NULL;
}
/*-------------------------------------------------------------------------*/
void *
dbgmalloc(size_t size, const char* func, const char* file, const int line)
{
  int msize = size + MAX_LOCATION_STRLEN;
  char *p;

  if ((p = (char *)malloc(msize)) != NULL) {
    increase_alloc_count(p, func, file, line);
    return (void *)(p + MAX_LOCATION_STRLEN);
  }
  return NULL;
}
/*-------------------------------------------------------------------------*/
void *
dbgrealloc(void *ptr, size_t size, 
	   const char* func, const char* file, const int line)
{
  int msize = size + MAX_LOCATION_STRLEN;
  char *p;

  if (ptr != NULL) {
    ptr -= MAX_LOCATION_STRLEN;
    decrease_alloc_count(ptr, func, file, line);
  }

  if ((p = (char *)realloc(ptr, msize)) != NULL) {
    increase_alloc_count(p, func, file, line);
    return (void *)(p + MAX_LOCATION_STRLEN);
  }
  return NULL;
}
/*-------------------------------------------------------------------------*/
char *
dbgstrdup(const char *s, const char* func, const char* file, const int line)
{
  int msize = strlen(s) + 1 + MAX_LOCATION_STRLEN;
  char *p;

  if ((p = (char *)malloc(msize))) {
    increase_alloc_count(p, func, file, line);
    p += MAX_LOCATION_STRLEN;
    strcpy(p, s);
    return p;
  }
  return NULL;
}
/*-------------------------------------------------------------------------*/
void 
dbgfree(void *ptr, const char* func, const char* file, const int line)
{
  /* should free the original pointer */
  char* chptr = (char *)ptr;

  chptr -= MAX_LOCATION_STRLEN;
  decrease_alloc_count(chptr, func, file, line);
  free(chptr);
}
#endif

