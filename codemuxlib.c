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
#include "debug.h"
#include "codemuxlib.h"

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
