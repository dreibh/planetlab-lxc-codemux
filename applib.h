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
#include "appdef.h"

float DiffTimeVal(const struct timeval *start, const struct timeval *end);

int  CreatePrivateAcceptSocketEx(int portNum, 
				 int nonBlocking, int loopbackOnly);
int CreatePrivateAcceptSocket(int portNum, int nonBlocking);
int CreatePublicUDPSocket(int portNum);
int MakeLoopbackConnection(int portNum, int nonBlocking);
int MakeConnection(char *name, in_addr_t netAddr, 
		   int portNum, int nonBlocking);

char *GetField(const unsigned char *start, int whichField);
char *GetWord(const unsigned char *start, int whichWord);
char *GetWordEx(const unsigned char *start, int whichWord, 
		char* dest, int max);
int WordCount(char *buf);
char *ZapSpacesAndZeros(char *src);

int Base36Digit(int a);
int Base36To10(int a);
int PopCount(int val);
int PopCountChar(int val);
int LogValChar(int val);

const char *StringOrNull(const char *s);
char *strchars(const char *string, const char *list);
char *strnchars(const char *string, int length, const char *list);

#if defined(OS_FREEBSD) || defined(OS_DARWIN)
#define HAS_STRNSTR
#endif

#ifndef HAS_STRNSTR
char *strnstr(const char * s1, int s1_len, const char * s2);
#endif

char * strncasestr(const char * s1, int s1_len, const char * s2);
int strncspn(const char* string, int length, const char* reject);
char *strnchr(const char *string, int length, const char needle);
char *StrdupLower(const char *orig);
void StrcpyLower(char *dest, const char *src);
void StrcpyLowerExcept(char *dest, int dest_max, const char* src, const char* except);
char *GetLowerNextLine(FILE *file);
char *GetNextLine(FILE *file);
char *GetNextLineNoCommStrip(FILE *file);
int DoesSuffixMatch(char *start, int len, char *suffix);
int DoesDotlessSuffixMatch(char *start, int len, char *suffix);

/* resolves a name using CoDNS */
#include "codns.h"
void HtoN_LocalQueryInfo(LocalQueryInfo *p);
void NtoH_LocalQueryResult(LocalQueryResult* p);
int CoDNSGetHostByNameSync(const char *name, struct in_addr *res);

#ifdef OS_LINUX
#include <alloca.h>
#endif

#include <string.h>

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

/* release memory pointer after checking NULL */
#define FREE_PTR(x) if ((x) != NULL) { xfree(x);}

/* Bit vector implementation */
void SetBits(int* bits, int idx, int maxNum);
int GetBits(int* bits, int idx, int maxNum);
int GetNumBits(int* bitvecs, int maxNum);

/* extended logging */
typedef void* HANDLE;             

HANDLE CreateLogFHandle(const char* signature, int change_file_name_on_save);
int OpenLogF(HANDLE file);
int WriteLog(HANDLE file, const char* data, int size, int forceFlush);
void DailyReopenLogF(HANDLE file);
int HandleToFileNo(HANDLE file);

/* flush the buffer */
#define FlushLogF(h)  WriteLog(h, NULL, 0, TRUE)

/* maximum single log file size, defined in applib.c */
extern int maxSingleLogSize; 

#include "gettimeofdayex.h"

void FeedbackDelay(struct timeval *start, float minSec, float maxSec);

int NiceExitOpenLog(char *logName);
void NiceExitBack(int val, char *reason, char *file, int line) __attribute__ ((noreturn));
#define NiceExit(val, reason) NiceExitBack(val, reason, __FILE__, __LINE__)

char *ReadFile(const char *filename);
char *ReadFileEx(const char *filename, int *size);
char *MmapFile(const char *filename, int *size);

/* use 32-bit unsigned integer, higher bits are thrown away */
#define WORD_BITS 32
#define MASK 0xFFFFFFFF
/* bitwise left rotate operator */
#define _rotl(Val, Bits) ((((Val)<<(Bits)) | (((Val) & MASK)>>(32 - (Bits)))) & MASK)

unsigned int HashString(const char *name, unsigned int hash, 
			int endOnQuery, int skipLastIfDot);
unsigned int CalcAgentHash(const char* agent);
#endif

