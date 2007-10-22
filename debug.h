#ifndef _DEBUG_H_
#define _DEBUG_H_
#include <stdio.h>
#undef max


/*
  TRACE  : print with function name
  TRACE0 : print without function name
  TRACE1 : print "buf" whose size is "size"
*/

//#define DEBUG
#ifdef DEBUG
extern HANDLE hdebugLog;
extern int defaultTraceSync;
#define TRACE0(fmt, msg...) {                                              \
       char __buf[2048];                                                   \
       if (hdebugLog) {                                                    \
          snprintf(__buf, sizeof(__buf), fmt, ##msg);                      \
          WriteLog(hdebugLog, __buf, strlen(__buf), defaultTraceSync);     \
       }                                                                   \
}          
#define TRACE1(buf, size) {                                                \
       WriteLog(hdebugLog, buf, size, defaultTraceSync);                   \
}
#define TRACE(fmt, msg...) {                                               \
       char __buf[2048];                                                   \
       if (hdebugLog) {                                                    \
         snprintf(__buf, sizeof(__buf), "[%s] " fmt, __FUNCTION__, ##msg); \
         WriteLog(hdebugLog, __buf, strlen(__buf), defaultTraceSync);      \
       }						                   \
}                                                                  
#define TRACEX(fmt) {                                                      \
       char __buf[2048];                                                   \
       if (hdebugLog) {                                                    \
         snprintf(__buf, sizeof(__buf), "[%s] " fmt, __FUNCTION__);        \
         WriteLog(hdebugLog, __buf, strlen(__buf), defaultTraceSync);      \
       }						                   \
}                                                                  
#else
#define TRACE0(fmt, msg...) (void)0
#define TRACE1(fmt, msg...) (void)0
#define TRACE(fmt, msg...)  (void)0
#define TRACEX(fmt)         (void)0
#endif // DEBUG

/*--------------------------------------------------------------
  macros used for debugging memory leaks 
  if DEBUG_MEMORY_LEAK is enabled, we track down all the memory
  allocation/freeing to count the number of allocations
 -------------------------------------------------------------*/
//#define DEBUG_MEMORY_LEAK

#ifndef DEBUG_MEMORY_LEAK

#define xcalloc(nmemb, size) calloc(nmemb, size)
#define xmalloc(size)        malloc(size)
#define xrealloc(ptr, size)  realloc(ptr, size)
#define xstrdup(s)           strdup(s)
#define xfree(ptr)           free(ptr)

#else

#ifndef DEBUG
#error "define DEBUG first to make DEBUG_MEMORY_LEAK work"
#endif

#define xcalloc(nmemb, size) dbgcalloc(nmemb, size, __FUNCTION__, \
                                      __FILE__, __LINE__)
#define xmalloc(size)        dbgmalloc(size, __FUNCTION__,        \
                                       __FILE__, __LINE__)
#define xrealloc(ptr, size)  dbgrealloc(ptr, size, __FUNCTION__,  \
                                        __FILE__, __LINE__)
#define xstrdup(s)           dbgstrdup(s, __FUNCTION__, __FILE__, __LINE__)
#define xfree(ptr)           dbgfree(ptr, __FUNCTION__, __FILE__, __LINE__)

void *dbgcalloc(size_t nmemb, size_t size, 
		const char* func, const char* file, const int line);
void *dbgmalloc(size_t size, 
		const char* func, const char* file, const int line);
void *dbgrealloc(void *ptr, size_t size, 
		 const char* func, const char* file, const int line);
char *dbgstrdup(const char *s, 
		const char* func, const char* file, const int line);
void  dbgfree(void *ptr, const char* func, const char* file, const int line);
void  dbg_print_memtrace(void);

#endif /* DEBUG_MEMOTY_LEAK */

#endif /* _DEBUG_H_ */
