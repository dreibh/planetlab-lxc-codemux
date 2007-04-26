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

/* stripped version of applib.c for codemux */

char *GetNextLine(FILE *file);
int   WordCount(char *buf);
char *GetField(const char *start, int whichField);
char *GetWord(const char *start, int whichWord);
int   DoesDotlessSuffixMatch(char *start, int len, char *suffix);
int   CreatePrivateAcceptSocket(int portNum, int nonBlocking);
char *StrdupLower(const char *orig);
void  StrcpyLower(char *dest, const char *src);

/* nice exit support */
void  NiceExitBack(int val, char *reason, char *file, int line);
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

