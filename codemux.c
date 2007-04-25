#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "applib.h"

#define CONF_FILE "/etc/codemux/codemux.conf"
#define DEMUX_PORT 80
#define REAL_WEBSERVER_CONFLINE "* root 1080"
#define TARG_SETSIZE 4096

/* set aside some small number of fds for us, allow the rest for
   connections */
#define MAX_CONNS ((TARG_SETSIZE-20)/2)

/* no single service can take more than half the connections */
#define SERVICE_MAX (MAX_CONNS/2)

/* how many total connections before we get concerned about fairness
   among them */
#define FAIRNESS_CUTOFF (MAX_CONNS * 0.85)


typedef struct FlowBuf {
  int fb_refs;			/* num refs */
  char *fb_buf;			/* actual buffer */
  int fb_used;			/* bytes used in buffer */
} FlowBuf;
#define FB_SIZE 3800		/* max usable size */
#define FB_ALLOCSIZE 4000	/* extra to include IP address */

typedef struct SockInfo {
  int si_peerFd;		/* fd of peer */
  struct in_addr si_cliAddr;	/* address of client */
  int si_blocked;		/* are we blocked? */
  int si_needsHeaderSince;	/* since when are we waiting for a header */
  int si_whichService;		/* index of service */
  FlowBuf *si_readBuf;		/* read data into this buffer */
  FlowBuf *si_writeBuf;		/* drain this buffer for writing */
} SockInfo;

static SockInfo sockInfo[TARG_SETSIZE]; /* fd number of peer socket */

typedef struct ServiceSig {
  char *ss_host;		/* suffix in host */
  char *ss_slice;
  short ss_port;
  int ss_slicePos;		/* position in slices array */
} ServiceSig;

static ServiceSig *serviceSig;
static int numServices;
static int confFileReadTime;
static int now;

typedef struct SliceInfo {
  char *si_sliceName;
  int si_inUse;			/* do any services refer to this? */
  int si_numConns;
  int si_xid;
} SliceInfo;

static SliceInfo *slices;
static int numSlices;
static int numActiveSlices;
static int numTotalSliceConns;
static int anySliceXidsNeeded;

typedef struct OurFDSet {
  long __fds_bits[TARG_SETSIZE/32];
} OurFDSet;
static OurFDSet masterReadSet, masterWriteSet;
static int highestSetFd;
static int numNeedingHeaders;	/* how many conns waiting on headers? */

static int numForks;

HANDLE hdebugLog;

#ifndef SO_SETXID
#define SO_SETXID SO_PEERCRED
#endif
/*-----------------------------------------------------------------*/
static SliceInfo *
ServiceToSlice(int whichService)
{
  if (whichService < 0)
    return(NULL);
  return(&slices[serviceSig[whichService].ss_slicePos]);
}
/*-----------------------------------------------------------------*/
static void
DumpStatus(int fd)
{
  char buf[65535];
  char *start = buf;
  int i;
  int len;

  sprintf(start, 
	  "numForks %d, numActiveSlices %d, numTotalSliceConns %d\n"
	  "numNeedingHeaders %d, anySliceXidsNeeded %d\n",
	  numForks, numActiveSlices, numTotalSliceConns,
	  numNeedingHeaders, anySliceXidsNeeded);
  start += strlen(start);

  for (i = 0; i < numSlices; i++) {
    SliceInfo *si = &slices[i];
    sprintf(start, "Slice %d: %s xid %d, %d conns, inUse %d\n", 
	    i, si->si_sliceName, si->si_xid, si->si_numConns,
	    si->si_inUse);
    start += strlen(start);
  }

  for (i = 0; i < numServices; i++) {
    ServiceSig *ss = &serviceSig[i];
    sprintf(start, "Service %d: %s %s port %d, slice# %d\n", i, ss->ss_host,
	    ss->ss_slice, (int) ss->ss_port, ss->ss_slicePos);
    start += strlen(start);
  }

  len = start - buf;
  write(fd, buf, len);
}
/*-----------------------------------------------------------------*/
static void
GetSliceXids(void)
{
  /* walks through /etc/passwd, and gets the uid for every slice we
     have */
  FILE *f;
  char *line;
  int i;

  if (!anySliceXidsNeeded)
    return;

  for (i = 0; i < numSlices; i++) {
    SliceInfo *si = &slices[i];
    si->si_inUse = 0;
  }
  for (i = 0; i < numServices; i++) {
    SliceInfo *si = ServiceToSlice(i);
    if (si != NULL)
      si->si_inUse++;
  }  

  if ((f = fopen("/etc/passwd", "r")) == NULL)
    return;

  while ((line = GetNextLine(f)) != NULL) {
    char *temp;
    int xid;

    if ((temp = strchr(line, ':')) == NULL)
      continue;			/* weird line */
    *temp = '\0';		/* terminate slice name */
    temp++;
    if ((temp = strchr(temp+1, ':')) == NULL)
      continue;			/* weird line */
    if ((xid = atoi(temp+1)) < 1)
      continue;			/* weird xid */

    /* we've got a slice name and xid, let's try to match */
    for (i = 0; i < numSlices; i++) {
      if (slices[i].si_xid == 0 &&
	  strcasecmp(slices[i].si_sliceName, line) == 0) {
	slices[i].si_xid = xid;
	break;
      }
    }
  }

  /* assume service 0 is the root service, and don't check it since
     it'll have xid zero */
  anySliceXidsNeeded = FALSE;
  for (i = 1; i < numSlices; i++) {
    if (slices[i].si_xid == 0 && slices[i].si_inUse > 0) {
      anySliceXidsNeeded = TRUE;
      break;
    }
  }

  fclose(f);
}
/*-----------------------------------------------------------------*/
static void
SliceConnsInc(int whichService)
{
  SliceInfo *si = ServiceToSlice(whichService);

  if (si == NULL)
    return;
  numTotalSliceConns++;
  si->si_numConns++;
  if (si->si_numConns == 1)
    numActiveSlices++;
}
/*-----------------------------------------------------------------*/
static void
SliceConnsDec(int whichService)
{
  SliceInfo *si = ServiceToSlice(whichService);

  if (si == NULL)
    return;
  numTotalSliceConns--;
  si->si_numConns--;
  if (si->si_numConns == 0)
    numActiveSlices--;
}
/*-----------------------------------------------------------------*/
static int
WhichSlicePos(char *slice)
{
  /* adds the new slice if necessary, returns the index into slice
     array. Never change the ordering of existing slices */
  int i;
  static int numSlicesAlloc;

  for (i = 0; i < numSlices; i++) {
    if (strcasecmp(slice, slices[i].si_sliceName) == 0)
      return(i);
  }

  if (numSlices >= numSlicesAlloc) {
    numSlicesAlloc = MAX(8, numSlicesAlloc * 2);
    slices = realloc(slices, numSlicesAlloc * sizeof(SliceInfo));
  }

  memset(&slices[numSlices], 0, sizeof(SliceInfo));
  slices[numSlices].si_sliceName = strdup(slice);
  numSlices++;
  return(numSlices-1);
}
/*-----------------------------------------------------------------*/
static void
ReadConfFile(void)
{
  int numAlloc = 0;
  int num = 0;
  ServiceSig *servs = NULL;
  FILE *f;
  char *line = NULL;
  struct stat statBuf;
  int i;

  if (stat(CONF_FILE, &statBuf) != 0) {
    fprintf(stderr, "failed stat on codemux.conf\n");
    if (numServices)
      return;
    exit(-1);
  }
  if (statBuf.st_mtime == confFileReadTime)
    return;

  if ((f = fopen(CONF_FILE, "r")) == NULL) {
    fprintf(stderr, "failed reading codemux.conf\n");
    if (numServices)
      return;
    exit(-1);
  }

  /* conf file entries look like
     coblitz.codeen.org princeton_coblitz 3125
  */

  while (1) {
    ServiceSig serv;
    int port;
    if (line != NULL)
      free(line);

    /* on the first pass, put in a fake entry for apache */
    if (num == 0)
      line = strdup(REAL_WEBSERVER_CONFLINE);
    else {
      if ((line = GetNextLine(f)) == NULL)
	break;
    }

    memset(&serv, 0, sizeof(serv));
    if (WordCount(line) != 3) {
      fprintf(stderr, "bad line: %s\n", line);
      continue;
    }
    serv.ss_port = port = atoi(GetField(line, 2));
    if (port < 1 || port > 65535 || port == DEMUX_PORT) {
      fprintf(stderr, "bad port: %s\n", line);
      continue;
    }

    serv.ss_host = GetWord(line, 0);
    serv.ss_slice = GetWord(line, 1);
    if (num >= numAlloc) {
      numAlloc = MAX(numAlloc * 2, 8);
      servs = realloc(servs, numAlloc * sizeof(ServiceSig));
    }
    serv.ss_slicePos = WhichSlicePos(serv.ss_slice);
    if (slices[serv.ss_slicePos].si_inUse == 0 &&
	slices[serv.ss_slicePos].si_xid < 1)
      anySliceXidsNeeded = TRUE; /* if new/inactive, we need xid */
    servs[num] = serv;
    num++;
  }

  fclose(f);

  if (num == 1) {
    if (numServices == 0) {
      fprintf(stderr, "nothing found in codemux.conf\n");
      exit(-1);
    }
    return;
  }

  for (i = 0; i < numServices; i++) {
    free(serviceSig[i].ss_host);
    free(serviceSig[i].ss_slice);
  }
  free(serviceSig);
  serviceSig = servs;
  numServices = num;
  confFileReadTime = statBuf.st_mtime;
}
/*-----------------------------------------------------------------*/
static char *err400BadRequest =
"HTTP/1.0 400 Bad Request\r\n"
"Content-Type: text/html\r\n"
"\r\n"
"You are trying to access a PlanetLab node, and your\n"
"request header exceeded the allowable size. Please\n"
"try again if you believe this error is temporary.\n";
/*-----------------------------------------------------------------*/
static char *err503Unavailable =
"HTTP/1.0 503 Service Unavailable\r\n"
"Content-Type: text/html\r\n"
"\r\n"
"You are trying to access a PlanetLab node, but the service\n"
"seems to be unavailable at the moment. Please try again.\n";
/*-----------------------------------------------------------------*/
static char *err503TooBusy =
"HTTP/1.0 503 Service Unavailable\r\n"
"Content-Type: text/html\r\n"
"\r\n"
"You are trying to access a PlanetLab node, but the service\n"
"seems to be overloaded at the moment. Please try again.\n";
/*-----------------------------------------------------------------*/
static void
SetFd(int fd, OurFDSet *set)
{
  if (highestSetFd < fd)
    highestSetFd = fd;
  FD_SET(fd, set);
}
/*-----------------------------------------------------------------*/
static void
ClearFd(int fd, OurFDSet *set)
{
  FD_CLR(fd, set);
}
/*-----------------------------------------------------------------*/
static int
RemoveHeader(char *lower, char *real, int totalSize, char *header)
{
  /* returns number of characters removed */
  char h2[256];
  int start, end, len;
  char *temp, *conn;

  sprintf(h2, "\n%s", header);

  if ((conn = strstr(lower, h2)) == NULL)
    return(0);

  conn++;
  /* determine how many characters to remove */
  if ((temp = strchr(conn, '\n')) != NULL)
    len = (temp - conn) + 1;
  else
    len = strlen(conn) + 1;
  start = conn - lower;
  end = start + len;
  memmove(&real[start], &real[end], totalSize - end);
  memmove(&lower[start], &lower[end], totalSize - end);

  return(len);
}
/*-----------------------------------------------------------------*/
static int
InsertHeader(char *buf, int totalSize, char *header)
{
  /* returns number of bytes inserted */
  
  char h2[256];
  char *temp;
  int len;
  
  sprintf(h2, "%s\r\n", header);
  len = strlen(h2);

  /* if we don't encounter a \n, it means that we have only a single
     line, and we'd converted the \n to a \0 */
  if ((temp = strchr(buf, '\n')) == NULL)
    temp = strchr(buf, '\0');
  temp++;
  
  memmove(temp + len, temp, totalSize - (temp - buf));
  memcpy(temp, h2, len);
  
  return(len);
}
/*-----------------------------------------------------------------*/
static int
FindService(FlowBuf *fb, int *whichService, struct in_addr addr)
{
  char *end;
  char *lowerBuf;
  char *hostVal;
  char *buf = fb->fb_buf;
  char orig[256];
#if 0
  char *url;
  int i;
  int len;
#endif
    
  if (strstr(buf, "\n\r\n") == NULL && strstr(buf, "\n\n") == NULL)
    return(FAILURE);

  /* insert client info after first line */
  sprintf(orig, "X-CoDemux-Client: %s", inet_ntoa(addr));
  fb->fb_used += InsertHeader(buf, fb->fb_used + 1, orig);
    
  /* get just the header, so we can work on it */
  LOCAL_STR_DUP_LOWER(lowerBuf, buf);
  if ((end = strstr(lowerBuf, "\n\r\n")) == NULL)
    end = strstr(lowerBuf, "\n\n");
  *end = '\0';
  
  /* remove any existing connection, keep-alive headers, add ours */
  fb->fb_used -= RemoveHeader(lowerBuf, buf, fb->fb_used + 1, "keep-alive:");
  fb->fb_used -= RemoveHeader(lowerBuf, buf, fb->fb_used + 1, "connection:");
  fb->fb_used += InsertHeader(buf, fb->fb_used + 1, "Connection: close");
  InsertHeader(lowerBuf, fb->fb_used + 1, "connection: close");

  /* isolate host, see if it matches */
  if ((hostVal = strstr(lowerBuf, "\nhost:")) != NULL) {
    int i;
    hostVal += strlen("\nhost:");
    if ((end = strchr(hostVal, '\n')) != NULL)
      *end = '\0';
    if ((end = strchr(hostVal, ':')) != NULL)
      *end = '\0';
    while (isspace(*hostVal))
      hostVal++;
    if (strlen(hostVal) > 0) {
      hostVal = GetWord(hostVal, 0);
      for (i = 1; i < numServices; i++) {
	if (serviceSig[i].ss_host != NULL &&
	    DoesDotlessSuffixMatch(hostVal, 0, serviceSig[i].ss_host)) {
	  *whichService = i;
	  free(hostVal);
	  /* printf("%s", buf); */
	  return(SUCCESS);
	}
      }
      free(hostVal);
    }
  }

#if 0
  /* see if URL prefix matches */
  if ((end = strchr(lowerBuf, '\n')) != NULL)
    *end = 0;
  if ((url = GetField(lowerBuf, 1)) == NULL ||
      url[0] != '/') {
    /* bad request - let apache handle it ? */
    *whichService = 0;
    return(SUCCESS);
  }
  url++;			/* skip the leading slash */
  for (i = 1; i < numServices; i++) {
    if (serviceSig[i].ss_prefix != NULL &&
	(len = strlen(serviceSig[i].ss_prefix)) > 0 &&
	strncmp(url, serviceSig[i].ss_prefix, len) == 0 &&
	(url[len] == ' ' || url[len] == '/')) {
      int startPos = url - lowerBuf;
      int stripLen = len + ((url[len] == '/') ? 1 : 0);
      /* strip out prefix */
      fb->fb_used -= stripLen;
      memmove(&buf[startPos], &buf[startPos+stripLen], 
	      fb->fb_used + 1 - startPos);
      /* printf("%s", buf); */
      *whichService = i;
      return(SUCCESS);
    }
  }
#endif

  /* default to first service */
  *whichService = 0;
  return(SUCCESS);
}
/*-----------------------------------------------------------------*/
static int
StartConnect(int origFD, int whichService)
{
  int sock;
  struct sockaddr_in dest;
  SockInfo *si;

  /* create socket */
  if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    return(FAILURE);
  }
  
  /* make socket non-blocking */
  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    close(sock);
    return(FAILURE);
  }
  
  /* set addr structure */
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(serviceSig[whichService].ss_port);
  dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  
  /* start connection process - we should be told that it's in
     progress */
  if (connect(sock, (struct sockaddr *) &dest, sizeof(dest)) != -1 || 
      errno != EINPROGRESS) {
    close(sock);
    return(FAILURE);
  }

  SetFd(sock, &masterWriteSet); /* determine when connect finishes */
  sockInfo[origFD].si_peerFd = sock;
  si = &sockInfo[sock];
  memset(si, 0, sizeof(SockInfo));
  si->si_peerFd = origFD;
  si->si_blocked = TRUE;	/* still connecting */
  si->si_whichService = whichService;
  si->si_writeBuf = sockInfo[origFD].si_readBuf;
  sockInfo[origFD].si_readBuf->fb_refs++;
  if (whichService >= 0)
    SliceConnsInc(whichService);

  return(SUCCESS);
}
/*-----------------------------------------------------------------*/
static int
WriteAvailData(int fd)
{
  SockInfo *si = &sockInfo[fd];
  FlowBuf *fb = si->si_writeBuf;
  int res;

  /* printf("trying to write fd %d\n", fd); */
  if (fb->fb_used < 1 || si->si_blocked)
    return(SUCCESS);

  /* printf("trying to write %d bytes\n", fb->fb_used); */
  /* write(STDOUT_FILENO, fb->fb_buf, fb->fb_used); */
  if ((res = write(fd, fb->fb_buf, fb->fb_used)) > 0) {
    fb->fb_used -= res;
    if (fb->fb_used > 0) {
      /* couldn't write all - assume blocked */
      memmove(fb->fb_buf, &fb->fb_buf[res], fb->fb_used);
      si->si_blocked = TRUE;
      SetFd(fd, &masterWriteSet);
    }
    /* printf("wrote %d\n", res); */
    return(SUCCESS);
  }

  /* we might have been full but didn't realize it */
  if (res == -1 && errno == EAGAIN) {
    si->si_blocked = TRUE;
    SetFd(fd, &masterWriteSet);
    return(SUCCESS);
  }

  /* otherwise, assume the worst */
  return(FAILURE);
}
/*-----------------------------------------------------------------*/
static OurFDSet socksToCloseVec;
static int numSocksToClose;
static int whichSocksToClose[TARG_SETSIZE];
/*-----------------------------------------------------------------*/
static void
CloseSock(int fd)
{
  if (FD_ISSET(fd, &socksToCloseVec))
    return;
  SetFd(fd, &socksToCloseVec);
  whichSocksToClose[numSocksToClose] = fd;
  numSocksToClose++;
}
/*-----------------------------------------------------------------*/
static void
DecBuf(FlowBuf *buf)
{
  if (buf == NULL)
    return;
  buf->fb_refs--;
  if (buf->fb_refs == 0) {
    free(buf->fb_buf);
    free(buf);
  }
}
/*-----------------------------------------------------------------*/
static void
ReallyCloseSocks(void)
{
  int i;

  memset(&socksToCloseVec, 0, sizeof(socksToCloseVec));

  for (i = 0; i < numSocksToClose; i++) {
    int fd = whichSocksToClose[i];
    close(fd);
    DecBuf(sockInfo[fd].si_readBuf);
    DecBuf(sockInfo[fd].si_writeBuf);
    ClearFd(fd, &masterReadSet);
    ClearFd(fd, &masterWriteSet);
    if (sockInfo[fd].si_needsHeaderSince) {
      sockInfo[fd].si_needsHeaderSince = 0;
      numNeedingHeaders--;
    }
    if (sockInfo[fd].si_whichService >= 0) {
      SliceConnsDec(sockInfo[fd].si_whichService);
      sockInfo[fd].si_whichService = -1;
    }
  }
  numSocksToClose = 0;
}
/*-----------------------------------------------------------------*/
static void
SocketReadyToRead(int fd)
{
  SockInfo *si = &sockInfo[fd];
  int spaceLeft;
  FlowBuf *fb;
  int res;

  /* if peer is closed, close ourselves */
  if (si->si_peerFd < 0 && (!si->si_needsHeaderSince)) {
    CloseSock(fd);
    return;
  }

  if ((fb = si->si_readBuf) == NULL) {
    fb = si->si_readBuf = calloc(1, sizeof(FlowBuf));
    fb->fb_refs = 1;
    if (si->si_peerFd >= 0) {
      sockInfo[si->si_peerFd].si_writeBuf = fb;
      fb->fb_refs = 2;
    }
  }

  if (fb->fb_buf == NULL)
    fb->fb_buf = malloc(FB_ALLOCSIZE);

  /* determine read buffer size - if 0, then block reads and return */
  if ((spaceLeft = FB_SIZE - fb->fb_used) < 0) {
    if (si->si_needsHeaderSince) {
      write(fd, err400BadRequest, strlen(err400BadRequest));
      CloseSock(fd);
      return;
    }
    else {
      ClearFd(fd, &masterReadSet);
      return;
    }
  }

  /* read as much as allowed, and is available */
  if ((res = read(fd, &fb->fb_buf[fb->fb_used], spaceLeft)) == 0) {
    CloseSock(fd);
    if (fb->fb_used == 0 && si->si_peerFd >= 0) {
      CloseSock(si->si_peerFd);
      si->si_peerFd = -1;
    }
    return;
  }
  if (res == -1) {
    if (errno == EAGAIN)
      return;
    CloseSock(fd);
    if (fb->fb_used == 0 && si->si_peerFd >= 0) {
      CloseSock(si->si_peerFd);
      si->si_peerFd = -1;
    }
    return;
  }
  fb->fb_used += res;
  fb->fb_buf[fb->fb_used] = 0;	/* terminate it for convenience */
  printf("sock %d, read %d, total %d\n", fd, res, fb->fb_used);

  /* if we need header, check if we've gotten it. if so, do
     modifications and continue. if not, check if we've read the
     maximum, and if so, fail */
  if (si->si_needsHeaderSince) {
    int whichService;
    SliceInfo *slice;

#define STATUS_REQ "GET /codemux/status.txt"
    if (strncasecmp(fb->fb_buf, STATUS_REQ, sizeof(STATUS_REQ)-1) == 0) {
      DumpStatus(fd);
      CloseSock(fd);
      return;
    }

    printf("trying to find service\n");
    if (FindService(fb, &whichService, si->si_cliAddr) != SUCCESS)
      return;
    printf("found service %d\n", whichService);
    slice = ServiceToSlice(whichService);

    /* no service can have more than some absolute max number of
       connections. Also, when we're too busy, start enforcing
       fairness across the servers */
    if (slice->si_numConns > SERVICE_MAX ||
	(numTotalSliceConns > FAIRNESS_CUTOFF && 
	 slice->si_numConns > MAX_CONNS/numActiveSlices)) {
      write(fd, err503TooBusy, strlen(err503TooBusy));
      CloseSock(fd);
      return;
    }

    if (slice->si_xid > 0) {
      setsockopt(fd, SOL_SOCKET, SO_SETXID, 
		 &slice->si_xid, sizeof(slice->si_xid));
      fprintf(stderr, "setsockopt() with XID = %d name = %s\n", 
	      slice->si_xid, slice->si_sliceName);
    }

    si->si_needsHeaderSince = 0;
    numNeedingHeaders--;
    if (StartConnect(fd, whichService) != SUCCESS) {
      write(fd, err503Unavailable, strlen(err503Unavailable));
      CloseSock(fd);
      return;
    }
    return;
  }

  /* write anything possible */
  if (WriteAvailData(si->si_peerFd) != SUCCESS) {
    /* assume the worst and close */
    CloseSock(fd);
    CloseSock(si->si_peerFd);
    si->si_peerFd = -1;
  }
}
/*-----------------------------------------------------------------*/
static void
SocketReadyToWrite(int fd)
{
  SockInfo *si = &sockInfo[fd];

  /* unblock it and read what it has */
  si->si_blocked = FALSE;
  ClearFd(fd, &masterWriteSet);
  SetFd(fd, &masterReadSet);
  
  /* enable reading on peer just in case it was off */
  if (si->si_peerFd >= 0)
    SetFd(si->si_peerFd, &masterReadSet);
    
  /* if we have data, write it */
  if (WriteAvailData(fd) != SUCCESS) {
   /* assume the worst and close */
    CloseSock(fd);
    if (si->si_peerFd >= 0) {
      CloseSock(si->si_peerFd);
      si->si_peerFd = -1;
    }
    return;
  }

  /* if peer is closed and we're done writing, we should close */
  if (si->si_peerFd < 0 && si->si_writeBuf->fb_used == 0)
    CloseSock(fd);
}
/*-----------------------------------------------------------------*/
static void
CloseReqlessConns(void)
{
  static int lastSweep;
  int maxAge;
  int i;

  if (lastSweep == now)
    return;
  lastSweep = now;

  if (numTotalSliceConns + numNeedingHeaders > MAX_CONNS ||
      numNeedingHeaders > TARG_SETSIZE/20) {
    /* second condition is probably an attack - close aggressively */
    maxAge = 5;
  }
  else if (numTotalSliceConns + numNeedingHeaders > FAIRNESS_CUTOFF ||
	   numNeedingHeaders > TARG_SETSIZE/40) {
    /* sweep a little aggressively */
    maxAge = 10;
  }
  else if (numNeedingHeaders > TARG_SETSIZE/80) {
    /* just sweep to close strays */
    maxAge = 30;
  }
  else {
    /* too little gained - not worth sweeping */
    return;
  }

  /* if it's too old, close it */
  for (i = 0; i < highestSetFd+1; i++) {
    if (sockInfo[i].si_needsHeaderSince &&
	(now - sockInfo[i].si_needsHeaderSince) > maxAge)
      CloseSock(i);
  }
}
/*-----------------------------------------------------------------*/
static void
MainLoop(int lisSock)
{
  int i;
  OurFDSet tempReadSet, tempWriteSet;
  int res;
  int lastConfCheck = 0;

  signal(SIGPIPE, SIG_IGN);

  while (1) {
    int newSock;
    int ceiling;
    struct timeval timeout;

    now = time(NULL);

    if (now - lastConfCheck > 300) {
      ReadConfFile();
      GetSliceXids();		/* always call - in case new slices created */
      lastConfCheck = now;
    }

    /* see if there's any activity */
    tempReadSet = masterReadSet;
    tempWriteSet = masterWriteSet;

    /* trim it down if needed */
    while (highestSetFd > 1 &&
	   (!FD_ISSET(highestSetFd, &tempReadSet)) &&
	   (!FD_ISSET(highestSetFd, &tempWriteSet)))
      highestSetFd--;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    res = select(highestSetFd+1, (fd_set *) &tempReadSet, 
		 (fd_set *) &tempWriteSet, NULL, &timeout);
    if (res < 0 && errno != EINTR) {
      perror("select");
      exit(-1);
    }

    now = time(NULL);

    /* clear the bit for listen socket to avoid confusion */
    ClearFd(lisSock, &tempReadSet);
    
    ceiling = highestSetFd+1;	/* copy it, since it changes during loop */
    /* pass data back and forth as needed */
    for (i = 0; i < ceiling; i++) {
      if (FD_ISSET(i, &tempWriteSet))
	SocketReadyToWrite(i);
    }
    for (i = 0; i < ceiling; i++) {
      if (FD_ISSET(i, &tempReadSet))
	SocketReadyToRead(i);
    }

    /* see if we need to close conns w/o requests */
    CloseReqlessConns();
    
    /* do all closes */
    ReallyCloseSocks();

    /* try accepting new connections */
    do {
      struct sockaddr_in addr;
      socklen_t lenAddr = sizeof(addr);
      if ((newSock = accept(lisSock, (struct sockaddr *) &addr, 
			    &lenAddr)) >= 0) {
	memset(&sockInfo[newSock], 0, sizeof(SockInfo));
	sockInfo[newSock].si_needsHeaderSince = now;
	numNeedingHeaders++;
	sockInfo[newSock].si_peerFd = -1;
	sockInfo[newSock].si_cliAddr = addr.sin_addr;
	sockInfo[newSock].si_whichService = -1;
	SetFd(newSock, &masterReadSet);
      }
    } while (newSock >= 0);
  }
}
/*-----------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
  int lisSock;

  if ((lisSock = CreatePrivateAcceptSocket(DEMUX_PORT, TRUE)) < 0) {
    fprintf(stderr, "failed creating accept socket\n");
    exit(-1);
  }
  SetFd(lisSock, &masterReadSet);

  while (1) {
    numForks++;
    if (fork()) {
      /* this is the parent - just wait */
      while (wait3(NULL, 0, NULL) < 1)
	;			/* just keep waiting for a real pid */
    }
    else {
      /* child process */
      MainLoop(lisSock);
      exit(-1);
    }
  }
}
/*-----------------------------------------------------------------*/
