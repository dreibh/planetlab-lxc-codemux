#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "applib.h"
#include "debug.h"
#include "codns.h"



int defaultTraceSync = TRUE;

/*-----------------------------------------------------------------*/
float
DiffTimeVal(const struct timeval *start, const struct timeval *end)
{
  struct timeval temp;
  if (end == NULL) {
    end = &temp;
    gettimeofday(&temp, NULL);
  }
  return(end->tv_sec - start->tv_sec + 
	 1e-6*(end->tv_usec - start->tv_usec));
}
/*-----------------------------------------------------------------*/
int 
CreatePrivateAcceptSocketEx(int portNum, int nonBlocking, int loopbackOnly)
{
  int doReuse = 1;
  struct linger doLinger;
  int sock;
  struct sockaddr_in sa;
  
  /* Create socket. */
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    return(-1);
  
  /* don't linger on close */
  doLinger.l_onoff = doLinger.l_linger = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER, 
		 &doLinger, sizeof(doLinger)) == -1) {
    close(sock);
    return(-1);
  }
  
  /* reuse addresses */
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
		 &doReuse, sizeof(doReuse)) == -1) {
    close(sock);
    return(-1);
  }

  if (nonBlocking) {
    /* make listen socket nonblocking */
    if (fcntl(sock, F_SETFL, O_NDELAY) == -1) {
      close(sock);
      return(-1);
    }
  }
  
  /* set up info for binding listen */
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = (loopbackOnly) ? htonl(INADDR_LOOPBACK) 
                                      : htonl(INADDR_ANY);
  sa.sin_port = htons(portNum);

  /* bind the sock */
  if (bind(sock, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
    close(sock);
    return(-1);
  }
  
  /* start listening */
  if (listen(sock, 32) == -1) {
    close(sock);
    return(-1);
  }
  
  return(sock);
}
/*-----------------------------------------------------------------*/
int
CreatePrivateAcceptSocket(int portNum, int nonBlocking)
{
  return CreatePrivateAcceptSocketEx(portNum, nonBlocking, FALSE);
}
/*-----------------------------------------------------------------*/
int
CreatePublicUDPSocket(int portNum)
{
  struct sockaddr_in hb_sin;
  int sock;

  if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    return(-1);

  memset(&hb_sin, 0, sizeof(hb_sin));
  hb_sin.sin_family = AF_INET;
  hb_sin.sin_addr.s_addr = INADDR_ANY;
  hb_sin.sin_port = htons(portNum);

  if (bind(sock, (struct sockaddr *) &hb_sin, sizeof(hb_sin)) < 0) {
    close(sock);
    return(-1);
  }
  return(sock);
}
/*-----------------------------------------------------------------*/
int
MakeConnection(char *name, in_addr_t netAddr, int portNum, int nonBlocking)
{
  struct sockaddr_in saddr;
  int fd;

  if (name != NULL) {
    struct hostent *ent;
    if ((ent = gethostbyname(name)) == NULL) {
      if (hdebugLog)
	TRACE("failed in name lookup - %s\n", name);
      return(-1);
    }
    memcpy(&netAddr, ent->h_addr, sizeof(netAddr));
  }

  if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    if (hdebugLog)
      TRACE("failed creating socket - %d\n", errno);
    return(-1);
  }

  if (nonBlocking) {
    if (fcntl(fd, F_SETFL, O_NDELAY) < 0) {
      if (hdebugLog)
	TRACE("failed fcntl'ing socket - %d\n", errno);
      close(fd);
      return(-1);
    }
  }

  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = netAddr;
  saddr.sin_port = htons(portNum);
  
  if (connect(fd, (struct sockaddr *) &saddr, 
	      sizeof(struct sockaddr_in)) < 0) {
    if (errno == EINPROGRESS)
      return(fd);
    if (hdebugLog)
      TRACE("failed connecting socket - %d\n", errno);
    close(fd);
    return(-1);
  }

  return(fd);
}
/*-----------------------------------------------------------------*/
int
MakeLoopbackConnection(int portNum, int nonBlocking)
{
  return(MakeConnection(NULL, htonl(INADDR_LOOPBACK), 
			portNum, nonBlocking));
}
/*-----------------------------------------------------------------*/
char *
GetField(const unsigned char *start, int whichField)
{
  int currField;

  /* move to first non-blank char */
  while (isspace(*start))
    start++;

  if (*start == '\0')
    return(NULL);

  for (currField = 0; currField < whichField; currField++) {
    /* move over this field */
    while (*start != '\0' && (!isspace(*start)))
      start++;
    /* move over blanks before next field */
    while (isspace(*start))
      start++;
    if (*start == '\0')
      return(NULL);
  }
  return((char *) start);
}
/* ---------------------------------------------------------------- */
char *
GetWord(const unsigned char *start, int whichWord)
{
  /* returns a newly allocated string containing the desired word,
     or NULL if there was a problem */
  unsigned char *temp;
  int len = 0;
  char *res;

  temp = (unsigned char *) GetField(start, whichWord);
  if (!temp)
    return(NULL);
  while (!(temp[len] == '\0' || isspace(temp[len])))
    len++;
  if (!len)
    NiceExit(-1, "internal error");
  res = (char *)xcalloc(1, len+1);
  if (!res) 
    NiceExit(-1, "out of memory");
  memcpy(res, temp, len);
  return(res);
}
/* ---------------------------------------------------------------- */
char *
GetWordEx(const unsigned char *start, int whichWord, 
	  char* dest, int max)
{
  /* returns a newly allocated string containing the desired word,
     or NULL if there was a problem */
  unsigned char *temp;
  int len = 0;

  memset(dest, 0, max);
  temp = (unsigned char *) GetField(start, whichWord);
  if (!temp)
    return(NULL);
  while (!(temp[len] == '\0' || isspace(temp[len])))
    len++;
  if (!len)
    NiceExit(-1, "internal error");
  if (len >= max-1)
    len = max-1;
  memcpy(dest, temp, len);
  return(dest);
}
/*-----------------------------------------------------------------*/
int
Base36Digit(int a)
{
  if (a < 0)
    return('0');
  if (a > 35)
    return('z');
  if (a < 10)
    return('0' + a);
  return('a' + a-10);
}
/*-----------------------------------------------------------------*/
int
Base36To10(int a)
{
  if (a >= '0' && a <= '9')
    return(a - '0');
  if (a >= 'a' && a <= 'z')
    return(10 + a - 'a');
  if (a >= 'A' && a <= 'Z')
    return(10 + a - 'A');
  return(0);
}
/*-----------------------------------------------------------------*/
int
PopCount(int val)
{
  int i;
  int count = 0;

  for (i = 0; i < (int)sizeof(int) * 8; i++) {
    if (val & (1<<i))
      count++;
  }
  return(count);
}
/*-----------------------------------------------------------------*/
int
PopCountChar(int val)
{
  int i;
  int count = 0;

  for (i = 0; i < (int)sizeof(int) * 8; i++) {
    if (val & (1<<i))
      count++;
  }
  return(Base36Digit(count));
}
/*-----------------------------------------------------------------*/
int
LogValChar(int val)
{
  int i;

  for (i = 0; i < 32; i++) {
    if (val <= (1<<i))
      return(Base36Digit(i));
  }
  return(Base36Digit(32));
}
/*-----------------------------------------------------------------*/
const char *
StringOrNull(const char *s)
{
  if (s)
    return(s);
  return("(null)");
}
/*-----------------------------------------------------------------*/
char *
strchars(const char *string, const char *list)
{
  /* acts like strchr, but works with multiple characters */
  int numChars = strlen(list);
  int i;
  const char *walk;

  if (numChars < 1)
    return(NULL);
  
  for (walk = string; *walk; walk++) {
    for (i = 0; i < numChars; i++) {
      if (*walk == list[i])
	return (char *)(walk);
    }
  }
  return(NULL);
}
/*-----------------------------------------------------------------*/
char *
strnchars(const char *string, int length, const char *list)
{
  /* acts like strchr, but works with multiple characters, and 
     reads exactly length characters from string.  */
  int searchingfor[256] = {0};
  const char *walk;

  if ('\0' == *list)
    return(NULL);

  for (; *list; list++) {
    /*
     * Be careful with this cast.
     * If *list == (char) -98 (extended ascii), then
     *     (unsigned)*list == (unsigned int)*list == 4294967198
     *     (int)(unsigned char)*list == (unsigned char)*list == 158
     * The compiler automatically casts the character to an int before
     * doing the array index.
     */
    searchingfor[(int)(unsigned char)*list] = 1;
  }

  for (walk = string; walk - string < length; walk++) {
    /* likewise */
    if (searchingfor[(int)(unsigned char)*walk]) {
	return (char *)(walk);
    }
  }
  return(NULL);
}
/*-----------------------------------------------------------------*/
int
strncspn(const char* string, int length, const char* reject)
     /* like strcspn but reads to length characters */
{
  int count = 0;
  int searchingfor[256] = {0};
  const char* walk;

  for (; *reject; reject++) {
    /*
     * Be careful with this cast.
     * If *list == (char) -98 (extended ascii), then
     *     (unsigned)*list == (unsigned int)*list == 4294967198
     *     (int)(unsigned char)*list == (unsigned char)*list == 158
     * The compiler automatically casts the character to an int before
     * doing the array index.
     */
    searchingfor[(int)(unsigned char)*reject] = 1;
  }

  for(walk = string; walk - string < length; walk++) {
    /* likewise */
    if (searchingfor[(int)(unsigned char)*walk]) {
      break;
    } else {
      count++;
    }
  }
  return count;
}
/*-----------------------------------------------------------------*/
char *
strnchr(const char *string, int length, const char needle)
{
  /* acts like strchr, but 
     reads exactly length characters from string.  */
  const char *walk;

  for (walk = string; walk - string < length; walk++) {
    if (needle == *walk) {
	return (char *)(walk);
    }
  }
  return(NULL);
}

/*-----------------------------------------------------------------*/
#ifndef HAS_STRNSTR
/* same as strstr, except that si doesn't have to end at '\0' */
char * strnstr(const char * s1, int s1_len, const char * s2)
{
  int l1, l2;
  
  l2 = strlen(s2);
  if (!l2)
    return (char *) s1;
  
  l1 = s1_len;
  while (l1 >= l2) {
    l1--;
    if (!memcmp(s1,s2,l2))
      return (char *) s1;
    s1++;
  }
  return NULL;
}
#endif
/*-----------------------------------------------------------------*/
/* same as strnstr, except case insensitive */
char * strncasestr(const char * s1, int s1_len, const char * s2)
{
  int l1, l2;
  
  l2 = strlen(s2);
  if (!l2)
    return (char *) s1;
  
  l1 = s1_len;
  while (l1 >= l2) {
    l1--;
    if (!strncasecmp(s1,s2,l2))
      return (char *) s1;
    s1++;
  }
  return NULL;
}

/*-----------------------------------------------------------------*/
char *
StrdupLower(const char *orig)
{
  char *temp;
  int i;

  if ((temp = xstrdup(orig)) == NULL)
    NiceExit(-1, "no memory in strduplower");
  for (i = 0; temp[i]; i++) {
    if (isupper((int) temp[i]))
      temp[i] = tolower(temp[i]);
  }
  return(temp);
}

/*-----------------------------------------------------------------*/
void 
StrcpyLower(char *dest, const char *src)
{
  /* copy 'src' to 'dest' in lower cases. 
     'dest' should have enough free space to hold src */
  int i;

  for (i = 0; src[i]; i++) {
    dest[i] = (isupper((int) src[i])) ? tolower(src[i]) : src[i];
  }

  /* mark it as NULL */
  dest[i] = 0;
}
/*-----------------------------------------------------------------*/
void
StrcpyLowerExcept(char *dest, int dest_max, const char* src, const char* except)
{
  /* copy 'src' to 'dest' in lower cases, skipping the chars in except.
     'dest' should have enough free space to hold src */
  int i, j;

  if (src == NULL)
    return;
  
  for (i = 0, j= 0; src[i]; i++) {
    if (strchr(except, src[i]))
      continue;

    if (j == dest_max - 1)
      break;
    dest[j++] = (isupper((int) src[i])) ? tolower(src[i]) : src[i];
  }

  /* mark it as NULL */
  dest[j] = 0;
}

/*-----------------------------------------------------------------*/
static char *
GetNextLineBack(FILE *file, int lower, int stripComments)
{
  /* reads the next non-blank line of the file. strips off any leading
     space, any comments, and any trailing space.  returns a lowercase
     version of the line that has been malloc'd */
  char line[1024];

  while (fgets(line, sizeof(line), file) != NULL) {
    char *temp;
    int len;

    /* strip off any comments, leading and trailing whitespace */
    if (stripComments) {
      if ((temp = strchr(line, '#')) != NULL)
	*temp = 0;
    }
    len = strlen(line);
    while (len > 0 && isspace((int) line[len-1])) {
      len--;
      line[len] = 0;
    }
    temp = line;
    while (isspace((int) *temp))
      temp++;
    if (temp[0] == 0)
      continue;			/* blank line, move on */

    if (lower)
      return(StrdupLower(temp));
    return(xstrdup(temp));
  }

  return(NULL);
}
/*-----------------------------------------------------------------*/
char *
GetLowerNextLine(FILE *file)
{
  return(GetNextLineBack(file, TRUE, TRUE));
}
/*-----------------------------------------------------------------*/
char *
GetNextLine(FILE *file)
{
  return(GetNextLineBack(file, FALSE, TRUE));
}
/*-----------------------------------------------------------------*/
char *
GetNextLineNoCommStrip(FILE *file)
{
  return(GetNextLineBack(file, FALSE, FALSE));
}
/*-----------------------------------------------------------------*/
int
DoesSuffixMatch(char *start, int len, char *suffix)
{
  int sufLen = strlen(suffix);

  if (len < 1)
    len = strlen(start);
  if (len < sufLen)
    return(FALSE);
  if (strncasecmp(start+len-sufLen, suffix, sufLen))
    return(FALSE);
  return(TRUE);
}
/*-----------------------------------------------------------------*/
int
DoesDotlessSuffixMatch(char *start, int len, char *suffix)
{
  /* ignores any dots on end of start, suffix */
  int sufLen = strlen(suffix);

  if (len < 1)
    len = strlen(start);

  while (len > 1 && start[len-1] == '.')
    len--;
  while (sufLen > 1 && suffix[sufLen-1] == '.')
    sufLen--;

  if (len < sufLen)
    return(FALSE);
  if (strncasecmp(start+len-sufLen, suffix, sufLen))
    return(FALSE);
  return(TRUE);
}

/*-----------------------------------------------------------------*/
/*                                                                 */
/* Bit Vector Implementation                                       */
/*                                                                 */
/*-----------------------------------------------------------------*/
#define BIT_INDEX (0x0000001F)

void
SetBits(int* bits, int idx, int maxNum)
{
  if (idx > (maxNum << 5)) {
    TRACE("Invalid index: %d", idx);
    return;
  }
  bits[(idx >> 5)] |= (1 << (idx & BIT_INDEX));
}
/*-----------------------------------------------------------------*/
int
GetBits(int* bits, int idx, int maxNum)
{
  if (idx > (maxNum << 5)) {
    TRACE("Invalid index: %d", idx);
    return FALSE;
  }
  return (bits[(idx >> 5)] & (1 << (idx & BIT_INDEX)));
}

/*-----------------------------------------------------------------*/
static inline int
GetNumBits_I(int bitvec)
{
  int i, count;

  for (i = 0, count = 0; i < 32; i++)
    if (bitvec & (1 << i)) count++;
  return count;
}

/*-----------------------------------------------------------------*/
int 
GetNumBits(int* bitvecs, int maxNum)
{
  int i, count;

  /* get the number of bits that have been set to 1 */
  for (i = 0, count = 0; i < maxNum; i++)
    count += GetNumBits_I(bitvecs[i]);
  return count;
}

/*-----------------------------------------------------------------*/

/* Logging & Trace support */

/* buffer threshold : when the size hits this value, it flushes its content
   to the file  */
#define LOG_BYTES_THRESHOLD (32*1024)

/* this flag indicates that it preserves the base file name for the current
   one, and changes its name when it actually closes it off */
#define CHANGE_FILE_NAME_ON_SAVE 0x01 

/* size of the actual log buffer */
#define LOG_BYTES_MAX       (2*LOG_BYTES_THRESHOLD)

/* log/trace book keeping information */
typedef struct ExtendedLog {
  char buffer[LOG_BYTES_MAX]; /* 64 KB */
  int  bytes;           /* number of bytes written */
  int  filesize;        /* number of bytes written into this file so far */
  int  fd;              /* file descriptor */
  char* sig;            /* base file name */
  int flags;            /* flags */
  time_t nextday;
} ExtendedLog, *PExtendedLog;

/* maximum single file size */
int maxSingleLogSize = 100 * (1024*1024);

static time_t
GetNextLogFileName(char* file, int size, const char* sig)
{
#define COMPRESS_EXT1 ".bz2"
#define COMPRESS_EXT2 ".gz"

  struct tm cur_tm;
  time_t cur_t;
  int idx = 0;

  cur_t = timeex(NULL);
  cur_tm = *gmtime(&cur_t);

  for (;;) {
    /* check if .bz2 exists */
    snprintf(file, size, "%s.%04d%02d%02d_%03d%s", 
	     sig, cur_tm.tm_year+1900, cur_tm.tm_mon+1, cur_tm.tm_mday, 
	     idx, COMPRESS_EXT1);

    if (access(file, F_OK) == 0) {
      idx++;
      continue;
    }

    /* check if .gz exists */
    snprintf(file, size, "%s.%04d%02d%02d_%03d%s", 
	     sig, cur_tm.tm_year+1900, cur_tm.tm_mon+1, cur_tm.tm_mday, 
	     idx++, COMPRESS_EXT2);

    if (access(file, F_OK) == 0) 
      continue;

    /* strip the extension and see if the (uncompressed) file exists */
    file[strlen(file) - sizeof(COMPRESS_EXT2) + 1] = 0;
    if (access(file, F_OK) != 0)
      break;
  }
  
  /* calculate & return the next day */
  cur_t -= (3600*cur_tm.tm_hour + 60*cur_tm.tm_min + cur_tm.tm_sec);
  return cur_t + 60*60*24;

#undef COMPRESS_EXT1
#undef COMPRESS_EXT2
}

/*-----------------------------------------------------------------*/
static void
FlushBuffer(HANDLE file) 
{
  /* write data into the file */
  ExtendedLog* pel = (ExtendedLog *)file;
  int written;

  if (pel == NULL || pel->fd < 0)
    return;
  
  if ((written = write(pel->fd, pel->buffer, pel->bytes)) > 0) {
    pel->bytes -= written;

    /* if it hasn't written all data, then we need to move memory */
    if (pel->bytes > 0) 
      memmove(pel->buffer, pel->buffer + written, pel->bytes);
    pel->buffer[pel->bytes] = 0;
    pel->filesize += written;
  }
  
  /* if the filesize is bigger than maxSignleLogSize, then close it off */
  if (pel->filesize >= maxSingleLogSize) 
    OpenLogF(file);
}

/*-----------------------------------------------------------------*/
HANDLE
CreateLogFHandle(const char* signature, int change_file_name_on_save)
{
  ExtendedLog* pel;

  if ((pel = (ExtendedLog *)xcalloc(1, sizeof(ExtendedLog))) == NULL)
    NiceExit(-1, "failed");

  pel->fd = -1;
  pel->sig = xstrdup(signature);
  if (pel->sig == NULL)
    NiceExit(-1, "signature copying failed");
  if (change_file_name_on_save)
    pel->flags |= CHANGE_FILE_NAME_ON_SAVE;

  return pel;
}


/*-----------------------------------------------------------------*/
int
OpenLogF(HANDLE file)
{
  char filename[1024];
  ExtendedLog* pel = (ExtendedLog *)file;

  if (pel == NULL)
    return -1;

  if (pel->fd != -1) 
    close(pel->fd);

  pel->nextday = GetNextLogFileName(filename, sizeof(filename), pel->sig);

  /* change the file name only at saving time 
     use pel->sig as current file name         */
  if (pel->flags & CHANGE_FILE_NAME_ON_SAVE) {
    if (access(pel->sig, F_OK) == 0) 
      rename(pel->sig, filename);
    strcpy(filename, pel->sig);
  }

  /* file opening */
  if ((pel->fd = open(filename, O_RDWR | O_CREAT | O_APPEND, 
		      S_IRUSR | S_IWUSR)) == -1) {
    char errMessage[2048];
    sprintf(errMessage, "couldn't open the extended log file %s\n", filename);
    NiceExit(-1, errMessage);
  }

  /* reset the file size */
  pel->filesize = 0;
  return 0;
}

/*-----------------------------------------------------------------*/
int 
WriteLog(HANDLE file, const char* data, int size, int forceFlush)
{
  ExtendedLog* pel = (ExtendedLog *)file;

  /* if an error might occur, then stop here */
  if (pel == NULL || pel->fd < 0 || size > LOG_BYTES_MAX)
    return -1;

  if (data != NULL) {
    /* flush the previous data, if this data would overfill the buffer */
    if (pel->bytes + size >= LOG_BYTES_MAX) 
      FlushBuffer(file);

    /* write into the buffer */
    memcpy(pel->buffer + pel->bytes, data, size);
    pel->bytes += size;
  }

  /* need to flush ? */
  if ((forceFlush && (pel->bytes > 0)) || (pel->bytes >= LOG_BYTES_THRESHOLD))
    FlushBuffer(file);

  return 0;
}

/*-----------------------------------------------------------------*/
void
DailyReopenLogF(HANDLE file) 
{
  /* check if current file is a day long,
     opens another for today's file         */
  ExtendedLog* pel = (ExtendedLog *)file;

  if (pel && (timeex(NULL) >= pel->nextday)) {
    FlushLogF(file);               /* flush */
    OpenLogF(file);                /* close previous one & reopen today's */
  }
}
/*-----------------------------------------------------------------*/
int 
HandleToFileNo(HANDLE file)
{
  ExtendedLog* pel = (ExtendedLog *)file;

  return (pel != NULL) ? pel->fd : -1;
}
/*-----------------------------------------------------------------*/
#define TO_HOST_L(x) x = ntohl(x);
#define TO_NETWORK_L(x) x = htonl(x);
void 
HtoN_LocalQueryInfo(LocalQueryInfo *p)
{
  TO_NETWORK_L(p->lqi_size);
  TO_NETWORK_L(p->lqi_id);
  TO_NETWORK_L(p->lqi_cache);
}
/*----------------------------------------------------------------*/
void 
NtoH_LocalQueryResult(LocalQueryResult* p)
{
  TO_HOST_L(p->lq_id);
  TO_HOST_L(p->lq_ttl);
}
/*----------------------------------------------------------------*/
int
CoDNSGetHostByNameSync(const char *name, struct in_addr *res)
{
  static int fd = -1;
  LocalQuery query;
  int size;
  LocalQueryResult result;

  /* create a connection to CoDNS */
  if (fd == -1 && (fd = MakeLoopbackConnection(CODNS_PORT, 0)) < 0) {
    TRACE("CoDNS connection try has failed!\n");

    /* try to resolve names using gethostbyname() */
    {
      struct hostent* hp;
      if ((hp = gethostbyname(name)) == NULL)
	NiceExit(-1, "gethostbyname also failed!");
      if (res)
	*res = *(struct in_addr *)hp->h_addr;
    }
    return 0;
  }

  memset(&query, 0, sizeof(query));
  size = strlen(name) + 1;
  query.lq_info.lqi_size = size;      /* length of name */
  query.lq_info.lqi_cache = TRUE;
  strcpy(query.lq_name, name);
  size += sizeof(query.lq_info) + sizeof(query.lq_zero);

  /* send a query */
  HtoN_LocalQueryInfo(&query.lq_info);
  if (write(fd, &query, size) != size) {
    close(fd);
    fd  = -1;
    return(-1);
  }

  /* get answer */
  do {
    size = read(fd, &result, sizeof(result));
  } while (size == -1 && errno == EINTR);
  /*  close(fd); */
  NtoH_LocalQueryResult(&result);

  if (size != sizeof(result))
    return(-1);

  *res = result.lq_address[0];
  return 0;
}
/*-----------------------------------------------------------------*/
void
FeedbackDelay(struct timeval *start, float minSec, float maxSec)
{
  float diff = DiffTimeVal(start, NULL);
  if (diff < minSec)
    diff = minSec;
  if (diff > maxSec)
    diff = maxSec;
  usleep((unsigned int)(diff * 1e6));
}
/*-----------------------------------------------------------------*/
static int niceExitLogFD = -1;
int
NiceExitOpenLog(char *logName)
{
  /* log file to record exit reasons */
  if (niceExitLogFD >= 0)
    close(niceExitLogFD);

  niceExitLogFD = open(logName, O_WRONLY | O_APPEND | O_CREAT, 
		       S_IRUSR | S_IWUSR);
  return((niceExitLogFD >= 0) ? SUCCESS : FAILURE);
}
/*-----------------------------------------------------------------*/
void
NiceExitBack(int val, char *reason, char *file, int line)
{
  char buf[1024];
  time_t currT = time(NULL);

  sprintf(buf, "[%s, %d] %.24s %s\n", file, line, ctime(&currT), reason);
  if (hdebugLog) {
    TRACE("%s", buf);
    FlushLogF(hdebugLog);
  }
  else
    fprintf(stderr, "%s", buf);

  if (niceExitLogFD >= 0)
    write(niceExitLogFD, buf, strlen(buf));
  exit(val);
}
/*-----------------------------------------------------------------*/
int
WordCount(char *buf)
{
  int count = 0;
  int wasSpace = TRUE;

  while (*buf != '\0') {
    int isSpace = isspace(*buf);
    if (wasSpace && (!isSpace))
      count++;
    wasSpace = isSpace;
    buf++;
  }
  return(count);
}
/*-----------------------------------------------------------------*/
char * 
ReadFile(const char *filename)
{
  int dummySize;
  return(ReadFileEx(filename, &dummySize));
}
/*-----------------------------------------------------------------*/
char * 
ReadFileEx(const char *filename, int *size)
{
  /* allocate a buffer, read the file into it and
     return the buffer */
  char *content = NULL;
  struct stat buf;
  int fd;

  *size = -1;
  if (access(filename, R_OK) < 0 
      || stat(filename, &buf) < 0
      || (fd = open(filename, O_RDONLY)) < 0) {
    TRACE("opening captcha file %s failed\n", filename);
    exit(-1);
  }

  if ((content = (char *)xmalloc(buf.st_size + 1)) == NULL) {
    TRACE("memory alloc failed\n");
    exit(-1);
  }

  if (read(fd, content, buf.st_size) != buf.st_size) {
    TRACE("opening captcha test file failed\n");
    exit(-1);
  }
  close(fd);
  content[buf.st_size] = 0;
  *size = buf.st_size;
  return content;
}
/*-----------------------------------------------------------------*/
char * 
MmapFile(const char *filename, int *size)
{
  /* allocate a buffer, read the file into it and
     return the buffer */
  char *content = NULL;
  struct stat buf;
  int fd;

  *size = -1;
  if (access(filename, R_OK) < 0 
      || stat(filename, &buf) < 0
      || (fd = open(filename, O_RDONLY)) < 0) {
    TRACE("opening captcha file %s failed\n", filename);
    exit(-1);
  }

  content = (char *)mmap(NULL, buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (content != MAP_FAILED) {
    *size = buf.st_size;
    return content;
  }
  return(NULL);
}
/*-----------------------------------------------------------------*/
unsigned int 
HashString(const char *name, unsigned int hash, int endOnQuery, 
	   int skipLastIfDot)
{
  /* if endOnQuery, we stop the hashing when we hit the question mark.
     if skipLastIfDot, we check the last component of the path to see
     if it includes a dot, and it so, we skip it. if both are specified,
     we first try the query, and if that exists, we don't trim the path */

  int i;
  int len;
  char *temp;

  if (name == NULL)
    return 0;

  len = strlen(name);
  if (endOnQuery && (temp = strchr(name, '?')) != NULL)
    len = temp - name;
  else if (skipLastIfDot) {
    /* first, find last component by searching backward */
    if ((temp = strrchr(name, '/')) != NULL) {
      /* now search forward for the dot */
      if (strchr(temp, '.') != NULL)
	len = temp - name;
    }
  }

  for (i = 0; i < len; i ++)
    hash += (_rotl(hash, 19) + name[i]);

  return hash;
}
/*-------------------------------------------------------------*/
unsigned int
CalcAgentHash(const char* agent) 
{
  char p[strlen(agent)+1];
  int i;

  if (agent == NULL)
    return 0;

  /* we remove all spaces */
  for (i = 0; *agent; agent++) {
    if (isspace(*agent))
      continue;
    p[i++] = *agent;
  }
  p[i] = 0;

  return HashString(p, 0, FALSE, FALSE);
}
/*-----------------------------------------------------------------*/
char *
ZapSpacesAndZeros(char *src)
{
  /* get rid of excess spaces between words, and remove any trailing
     (post-decimal) zeros from floating point numbers */
  static char smallLine[4096];
  char *dst = smallLine;
  char *word;
  int addSpace = FALSE;

  while (src != NULL &&
	 (word = GetWord((const unsigned char*)src, 0)) != NULL) {
    char *temp;
    int isDotNumber = TRUE;

    src = GetField((const unsigned char*)src, 1);	/* advance to next */

    /* check to make sure it has exactly one decimal point */
    if ((temp = strchr(word, '.')) != NULL &&
	(temp = strchr(temp+1, '.')) == NULL) {
      /* make sure it's all digits or the dot */
      for (temp = word; *temp != '\0'; temp++) {
	if (!(isdigit(*temp) || *temp == '.')) {
	  isDotNumber = FALSE;
	  break;
	}
      }
    }
    else
      isDotNumber = FALSE;
    
    if (isDotNumber) {
      /* strip off any trailing zeros and possibly the decimal point */
      int len = strlen(word) - 1;
      
      while (word[len] == '0') {
	word[len] = '\0';
	len--;
      }
      if (word[len] == '.')
	word[len] = '\0';
    }

    if (addSpace)
      sprintf(dst, " %s", word);
    else
      sprintf(dst, "%s", word);
    dst += strlen(dst);
    addSpace = TRUE;
    xfree(word);
  }

  *dst = 0;
  return(smallLine);
}
/*-----------------------------------------------------------------*/
