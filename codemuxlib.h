#ifndef _APPLIB_H_
#define _APPLIB_H_

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <netdb.h>
#include <limits.h>
#include <string.h>
#ifdef OS_LINUX
#include <alloca.h>
#endif

#include "appdef.h"

typedef void* HANDLE;

/* stripped version of applib.c for codemux */

extern char *GetNextLine(FILE *file);
extern int   WordCount(char *buf);
extern char *GetField(const char *start, int whichField);
extern char *GetWord(const char *start, int whichWord);
extern int   DoesDotlessSuffixMatch(char *start, int len, char *suffix);
extern int   CreatePrivateAcceptSocket(int portNum, int nonBlocking, struct in_addr *addr);
extern char *StrdupLower(const char *orig);
extern void  StrcpyLower(char *dest, const char *src);

extern int OpenLogF(HANDLE file);
extern int WriteLog(HANDLE file, const char* data, int size, int forceFlush);
extern void DailyReopenLogF(HANDLE file);
extern unsigned int HashString(const char *name, unsigned int hash, 
			       int endOnQuery, int skipLastIfDot);

#define FlushLogF(h)  WriteLog(h, NULL, 0, TRUE)

#define MASK 0xFFFFFFFF
#define _rotl(Val, Bits) ((((Val)<<(Bits)) | (((Val) & MASK)>>(32 - (Bits)))) & MASK)

/* nice exit support */
extern void  NiceExitBack(int val, char *reason, char *file, int line);
#define NiceExit(val, reason) NiceExitBack(val, reason, __FILE__, __LINE__)


/*  allocate stack memory to copy "src" to "dest" in lower cases */
#define LOCAL_STR_DUP_LOWER(dest, src)    \
  { dest = alloca(strlen(src) + 1);       \
    StrcpyLower(dest, src);               \
  }
  
/*  allocate stack memory to copy "src" to "dest" */
#define LOCAL_STR_DUP(dest, src)         \
  { dest = alloca(strlen(src) + 1);      \
    strcpy(dest, src);                   \
  }
#endif

