#ifndef _CODNS_H_
#define _CODNS_H_
#include "ports.h"

/* query info - fixed part */
typedef struct LocalQueryInfo {
  int lqi_size;                 /* length of the name string */
  int lqi_id;                   /* query id */
  int lqi_cache;                /* not being used now */
} LocalQueryInfo;

/* query info + name 
   query structure expected from a client */
#define MAX_QUERY_NAME 256
#define SIG_SPLIT_TRANSACTION 0 /* signature for split-transaction */
typedef struct LocalQuery {
  int  lq_zero;                 /* always set to SIG_SPLIT_TRANSACTION(=0) */
  LocalQueryInfo lq_info;       /* query info */
  char lq_name[MAX_QUERY_NAME]; /* name */
} LocalQuery;

/* query result from CoDNS 
   we set MAX_ANSWERS for easy implementation.
   if lq.address[i].s_addr == 0, that means it returned i-1 valid anwers. */
#define MAX_ANSWERS 8         
typedef struct LocalQueryResult {
  int lq_id;                               /* query id */
  int lq_ttl;                              /* TTL of the record */
  struct in_addr lq_address[MAX_ANSWERS];  /* IP addresses for the query */
} LocalQueryResult;

/*----------------------------------------------------------------------*/

/* temporary section : from here to the end
   used for defining variables or constants for testing */

/* for testing in HBTWGET */
#define HBTWGET_CODNS_ID (-3)

#endif // _CODNS_H_


