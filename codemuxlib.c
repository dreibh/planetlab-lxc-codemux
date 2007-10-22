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
#include "codemuxlib.h"
#include "debug.h"

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
GetField(const char *start, int whichField)
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
GetWord(const char *start, int whichWord)
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
/*-----------------------------------------------------------------*/
static int 
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
NiceExitBack(int val, char *reason, char *file, int line)
{
  char buf[1024];
  time_t currT = time(NULL);

  sprintf(buf, "[%s, %d] %.24s %s\n", file, line, ctime(&currT), reason);
  fprintf(stderr, "%s", buf);
  exit(val);
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

  cur_t = time(NULL);
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
  char *temp;

  /* check if the file can be created */
  if ((temp = strrchr(signature, '/')) != NULL) {
    int dirlen = temp - signature + 1;
    char pardir[dirlen+1];

    memcpy(pardir, signature, dirlen);
    pardir[dirlen] = 0;
    if (access(pardir, W_OK) != 0) 
      return NULL;
  } else {
    /* assume it's the current directory */
    if (access("./", W_OK) != 0) 
      return NULL;
  }

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

  if (pel && (time(NULL) >= pel->nextday)) {
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
