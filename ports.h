#ifndef _PORTS_H_
#define _PORTS_H_

#define PORTS_COBLITZ

#ifdef PORTS_CODEEN

#define HOMEDIR "/home/princeton_codeen"
/* options in prox.conf or defined implicitly
                            1161 snmpPort
                           31415 APIDummyServPort
                            3130 ICPPort */
/* different ports for CoDNS and CoDNS2 */
#ifdef CODNS2
#define CODNS_PORT          24119 /* CODNS2 PORT */
#define CODNS_STAT_PORT     24118 /* CODNS2 STATISTICS PORT */
#define CODNS_HB_PORT       24121
#else
#define CODNS_PORT          4119 /* CODNS PORT */
#define CODNS_STAT_PORT     4118 /* CODNS STATISTICS PORT */
#define CODNS_HB_PORT       4121
#endif

#define HB_PORT            23127 /* CoDeeN heartbeat port */
#define CODEMON_PORT        2123
/*                          3124 proxy listen port */
#define CODEPLOY_PORT       2125
#define FAKE_WEBSERVER_PORT 3126
#define FAKE_WS_PORT_STR   "3126"
/*                          3127 proxy listen port */
/*                          3128 proxy listen port */
#define MAIN_PROXYPORT_STR "3128"
#define CODEEN_TM_PORT      3129
#define PROXY_PORT          3128
#define PROXY_PORT2         3127
#define PROXY_PORT3         3124

/* used by redir_helper.c */
#define QUERY_PORT          3122
#define RTTSNAP_PORT        3110

#elif defined(PORTS_COBLITZ)

#define HOMEDIR "/home/princeton_coblitz"
/* options in prox.conf
                            2111 snmpPort
                            2112 APIDummyServPort
                            2113 ICPPort */
#define CODNS_PORT          2119 /* CODNS PORT */
#define CODNS_STAT_PORT     2120 /* CODNS STATISTICS PORT */
#define CODNS_HB_PORT       2121
#define HB_PORT             2122 /* CoDeeN heartbeat port */
#define CODEMON_PORT       23126
/*                          2124 proxy listen port */
#define CODEPLOY_PORT       3125
#define FAKE_WEBSERVER_PORT 2126
#define FAKE_WS_PORT_STR   "2126"
/*                          2127 proxy listen port */
/*                          2128 proxy listen port */
#define MAIN_PROXYPORT_STR "2128"
#define CODEEN_TM_PORT      2129
#define PROXY_PORT          2128
#define PROXY_PORT2         2127
#define PROXY_PORT3         2124

/* used by redir_helper.c */
#define QUERY_PORT          2122
#define RTTSNAP_PORT        2110

#else
#error "need to define either PORTS_CODEEN or PORTS_COBLITZ"
#endif


#endif


/*

other changed files: prox_monitor prox.conf

 4119   tcp  415  princeton_codeen codns
 4119   udp  415  princeton_codeen codns
 4118   tcp  412  princeton_codeen codns
 4121   udp  411  princeton_codeen codns

23126   tcp  412  princeton_codeen codemon
23126   udp  412  princeton_codeen codemon
23127   tcp  399  princeton_codeen codeen
23127   udp  399  princeton_codeen codeen
 3126   tcp  399  princeton_codeen codeen
 1161   udp  401  princeton_codeen specified via snmpPort
31415   tcp  401  princeton_codeen can be specified via APIDummyServPort
 3124   tcp  399  princeton_codeen specified in prox.conf
 3127   tcp  399  princeton_codeen specified in prox.conf
 3128   tcp  398  princeton_codeen specified in prox.conf
 3129   tcp  399  princeton_codeen codeen traffic mon
 3130   udp  401  princeton_codeen can be specified from ICPPort
 3125   tcp  410  princeton_codeen codeploy
 3135   tcp  370  princeton_codeen

*/
