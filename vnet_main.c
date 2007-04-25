/*
 * VServer IP isolation.
 *
 * This file implements netfilter hooks and AF_INET socket function
 * overrides.
 *
 * Mark Huang <mlhuang@cs.princeton.edu>
 * Copyright (C) 2004 The Trustees of Princeton University
 *
 * $Id: vnet_main.c,v 1.40 2007/03/08 15:46:07 mef Exp $
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/pkt_sched.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/route.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_tables.h>

#include "vnet_config.h"
#include "vnet.h"
#include "vnet_dbg.h"
#include "vnet_compat.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)

#define HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX

#include <net/inet_hashtables.h>

static inline void
vnet_timewait_put(struct sock* sk)
{
         inet_twsk_put((struct inet_timewait_sock *)sk);
}

static inline struct sock* 
vnet_tcp_lookup(u32 src_ip, u16 src_port, 
		u32 ip, u16 port, int dif)
{
  return inet_lookup(&tcp_hashinfo, src_ip, src_port, ip, port, dif);
}

static inline int vnet_iif(const struct sk_buff *skb)
{
  return inet_iif(skb);
}
#endif

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,12)

#define HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX

static inline void 
vnet_timewait_put(struct sock* sk)
{
  /* net/tcp.h */
  tcp_tw_put((struct tcp_tw_bucket*)sk);
}

static inline struct sock* 
vnet_tcp_lookup(u32 saddr, u16 sport, u32 daddr,u16 dport, int dif)
{
  extern struct sock *tcp_v4_lookup(u32, u16, u32, u16, int);
  return tcp_v4_lookup(saddr, sport, daddr, dport, dif);
}

/* same as tcp_v4_iff() in net/ipv4/tcp_ipv4. */
static inline int vnet_iif(const struct sk_buff *skb)
{
	return ((struct rtable *)skb->dst)->rt_iif;
}
#endif

#ifndef HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX
#warning DEMUX FUNCTIONALITY NOT SUPPORTED
#endif

int vnet_verbose = 1;

/* We subdivide the 1: major class into 15 minor subclasses 1:1, 1:2,
 * etc. so that we can represent multiple bandwidth limits. The 1:1
 * subclass has children named 1:1000, 1:1001, etc., one for each
 * context (up to 4096). Similarly, the 1:2 subclass has children
 * named 1:2000, 1:2001, etc. By default, the 1:1 subclass represents
 * the node bandwidth cap and 1:1000 represents the root context's
 * share of it. */
int vnet_root_class = TC_H_MAKE(1 << 16, 0x1000);

#define FILTER_VALID_HOOKS ((1 << NF_IP_LOCAL_IN) | \
			    (1 << NF_IP_LOCAL_OUT) | \
			    (1 << NF_IP_POST_ROUTING))

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)

/* Standard entry. */
struct ipt_standard
{
	struct ipt_entry entry;
	struct ipt_standard_target target;
};

struct ipt_error_target
{
	struct ipt_entry_target target;
	char errorname[IPT_FUNCTION_MAXNAMELEN];
};

struct ipt_error
{
	struct ipt_entry entry;
	struct ipt_error_target target;
};

#endif

static struct
{
	struct ipt_replace repl;
	struct ipt_standard entries[3];
	struct ipt_error term;
} initial_table __initdata =
{
	.repl =
	{
		.name = "vnet",
		.valid_hooks = FILTER_VALID_HOOKS,
		.num_entries = 4,
		.size = sizeof(struct ipt_standard) * 3 + sizeof(struct ipt_error),
		.hook_entry = { [NF_IP_LOCAL_IN] = 0,
				[NF_IP_LOCAL_OUT] = sizeof(struct ipt_standard),
				[NF_IP_POST_ROUTING] = sizeof(struct ipt_standard) * 2, },
		.underflow = { [NF_IP_LOCAL_IN] = 0,
			       [NF_IP_LOCAL_OUT] = sizeof(struct ipt_standard),
			       [NF_IP_POST_ROUTING] = sizeof(struct ipt_standard) * 2, },
	},

	.entries =
	{
		/* LOCAL_IN: currently unused */
		{ .entry = { .target_offset = sizeof(struct ipt_entry),
			     .next_offset = sizeof(struct ipt_standard), },
		  .target = { .target = { .u = { .target_size = IPT_ALIGN(sizeof(struct ipt_standard_target)), }, },
			      .verdict = -NF_ACCEPT - 1, },
		},

		/* LOCAL_OUT: used for logging */
		{ .entry = { .target_offset = sizeof(struct ipt_entry),
			     .next_offset = sizeof(struct ipt_standard), },
		  .target = { .target = { .u = { .target_size = IPT_ALIGN(sizeof(struct ipt_standard_target)), }, },
			      .verdict = -NF_ACCEPT - 1, },
		},

		/* POST_ROUTING: used for priority classification */
		{ .entry = { .target_offset = sizeof(struct ipt_entry),
			     .next_offset = sizeof(struct ipt_standard), },
		  .target = { .target = { .u = { .target_size = IPT_ALIGN(sizeof(struct ipt_standard_target)), }, },
			      .verdict = -NF_ACCEPT - 1, },
		},
	},

	/* ERROR */
	.term =
	{
		.entry = { .target_offset = sizeof(struct ipt_entry),
			   .next_offset = sizeof(struct ipt_error), },
		.target = { .target = { .u = { .user = { .target_size = IPT_ALIGN(sizeof(struct ipt_error_target)),
							 .name = IPT_ERROR_TARGET, }, }, },
			    .errorname = "ERROR", },
	},
};

static struct ipt_table vnet_table = {
	.name		= "vnet",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
	.table		= &initial_table.repl,
#endif
	.valid_hooks	= FILTER_VALID_HOOKS,
	.lock		= RW_LOCK_UNLOCKED,
	.me		= THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
	.af		= AF_INET,
#endif
};

static inline u_int16_t
get_dst_port(struct ip_conntrack_tuple *tuple)
{
	switch (tuple->dst.protonum) {
	case IPPROTO_GRE:
		/* XXX Truncate 32-bit GRE key to 16 bits */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)		
		return tuple->dst.u.gre.key;
#else
		return htons(ntohl(tuple->dst.u.gre.key));
#endif
	case IPPROTO_ICMP:
		/* Bind on ICMP echo ID */
		return tuple->src.u.icmp.id;
	case IPPROTO_TCP:
		return tuple->dst.u.tcp.port;
	case IPPROTO_UDP:
		return tuple->dst.u.udp.port;
	default:
		return tuple->dst.u.all;
	}
}

static inline u_int16_t
get_src_port(struct ip_conntrack_tuple *tuple)
{
	switch (tuple->dst.protonum) {
	case IPPROTO_GRE:
		/* XXX Truncate 32-bit GRE key to 16 bits */
		return htons(ntohl(tuple->src.u.gre.key));
	case IPPROTO_ICMP:
		/* Bind on ICMP echo ID */
		return tuple->src.u.icmp.id;
	case IPPROTO_TCP:
		return tuple->src.u.tcp.port;
	case IPPROTO_UDP:
		return tuple->src.u.udp.port;
	default:
		return tuple->src.u.all;
	}
}



static unsigned int
vnet_hook(unsigned int hook,
	  struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	u_int8_t protocol;
	u_int32_t ip;
	u_int16_t port;
	struct bind_key *key;
	xid_t xid;
	unsigned int verdict;
	int priority;
	struct sock *sk;
	int need_to_free_sk = 0;

	ct = ip_conntrack_get(*pskb, &ctinfo);
	dir = CTINFO2DIR(ctinfo);

	/* Default to marking packet with root context ID */
	xid = 0;

	switch (hook) {

	case NF_IP_LOCAL_IN:
		/* Multicast to 224.0.0.1 is one example */
		if (!ct)
			break;

		/* Determine if the packet is destined for a bound port */
		protocol = ct->tuplehash[dir].tuple.dst.protonum;
		assert(ctinfo == IP_CT_RELATED ||
		       ctinfo == (IP_CT_IS_REPLY + IP_CT_RELATED) ||
		       protocol == (*pskb)->nh.iph->protocol);
		ip = ct->tuplehash[dir].tuple.dst.ip;
		port = get_dst_port(&ct->tuplehash[dir].tuple);

		/* Check if the port is bound */
		key = bind_get(protocol, ip, port, NULL);

		if (key && key->sk != NULL) {

			/* A new or established connection to a bound port */
			sk = key->sk;

#ifdef HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX
			/* If the bound socket is a real TCP socket, then the context that
			 * bound the port could have re-assigned an established connection
			 * socket to another context. See if this is the case.
			 */
			if (protocol == IPPROTO_TCP && sk->sk_type == SOCK_STREAM) {
				struct sock *tcp_sk;
				u_int32_t src_ip = ct->tuplehash[dir].tuple.src.ip;
				u_int16_t src_port = get_src_port(&ct->tuplehash[dir].tuple);

				tcp_sk = vnet_tcp_lookup(src_ip, src_port, ip, port, vnet_iif(*pskb));
				if (tcp_sk) {
				  if (tcp_sk->sk_state == TCP_TIME_WAIT) {
				     sock_put(tcp_sk);
				  } else {
				    dbg("vnet_in:%d: established TCP socket %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n", 
					get_sk_xid(tcp_sk), NIPQUAD(src_ip), ntohs(src_port), NIPQUAD(ip), ntohs(port));
				    sk = tcp_sk;
				    need_to_free_sk = 1;
				  }
				  /* Remember to sock_put()! */
				}
			}
#endif

			/* Indicate to the stack that the packet was "expected", so that it does
			 * not generate a TCP RST or ICMP Unreachable message. This requires a
			 * kernel patch.
			 */
			if (sk->sk_type == SOCK_RAW)
			  (*pskb)->sk = sk;

			assert(sk);
			xid = get_sk_xid(sk);

			/* Steal the reply end of the connection */
			if (get_ct_xid(ct, !dir) != xid) {
				dbg("vnet_in:%d: stealing %sbound %s connection %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u from context %d\n", xid,
				    key ? "" : "un", print_protocol(protocol),
				    NIPQUAD(ip), ntohs(port),
				    NIPQUAD(ct->tuplehash[!dir].tuple.dst.ip), ntohs(ct->tuplehash[!dir].tuple.dst.u.all),
				    get_ct_xid(ct, !dir));
				set_ct_xid(ct, !dir, xid);
			}

			/* Store the owner (if any) of the other side of the connection (if
			 * localhost) in the peercred struct.
			 */
			sk->sk_peercred.uid = sk->sk_peercred.gid = (__u32) get_ct_xid(ct, dir);

			if (ctinfo == IP_CT_NEW) {
				dbg("vnet_in: %s port %u.%u.%u.%u:%u bound by context %d\n",
				    print_protocol(protocol), NIPQUAD(ip), ntohs(port), xid);
			}

#ifdef HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX
			if (need_to_free_sk) {
			  /*
			  if (sk->sk_state == TCP_TIME_WAIT)
			    vnet_timewait_put(sk);
			  else*/
			  sock_put(sk);
			  need_to_free_sk=0;
			}
#endif
			bind_put(key);

		} else if ((int) get_ct_xid(ct, !dir) == -1) {
			/* A new connection to an unbound port */
			if (ctinfo == IP_CT_NEW) {
				dbg("vnet_in: %s port %u.%u.%u.%u:%u not bound\n",
				    print_protocol(protocol), NIPQUAD(ip), ntohs(port));
			}
		} else {
			/* A new or established connection to an unbound port that could be
			 * associated with an active socket ("could be" because the socket
			 * could be closed and the connection in a WAIT state). In any case,
			 * give it to the last owner of the connection.
			 */
			xid = get_ct_xid(ct, !dir);
		}

		break;

	case NF_IP_LOCAL_OUT:
		/* Get the context ID of the sender */
		assert((*pskb)->sk);
		xid = get_sk_xid((*pskb)->sk);

		/* Default class */
		priority = vnet_root_class;

		if (ct) {
			protocol = ct->tuplehash[dir].tuple.dst.protonum;
			assert(ctinfo == IP_CT_RELATED ||
			       ctinfo == (IP_CT_IS_REPLY + IP_CT_RELATED) ||
			       protocol == (*pskb)->nh.iph->protocol);
			ip = ct->tuplehash[dir].tuple.src.ip;
			assert(ctinfo == IP_CT_RELATED ||
			       ctinfo == (IP_CT_IS_REPLY + IP_CT_RELATED) ||
			       ip == __constant_htonl(INADDR_ANY) || ip == (*pskb)->nh.iph->saddr);
			port = get_src_port(&ct->tuplehash[dir].tuple);
		} else {
			protocol = port = 0;
		}

		if (xid) {
			/* Multicast to 224.0.0.1 is one example */
			if (!ct) {
				dbg("vnet_out:%d: dropping untrackable IP packet\n", xid);
				return NF_DROP;
			}

			/* XXX Is this guaranteed? */
			if ((*pskb)->len < sizeof(struct iphdr)) {
				dbg("vnet_out:%d: dropping runt IP packet\n", xid);
				return NF_DROP;
			}

			/* Check source IP address */
			if (inet_addr_type(ip) != RTN_LOCAL) {
				dbg("vnet_out:%d: non-local source IP address %u.%u.%u.%u not allowed\n", xid,
				    NIPQUAD(ip));
				return NF_DROP;
			}

			/* Sending of ICMP error messages not allowed */
			if (protocol == IPPROTO_ICMP) {
				struct icmphdr *icmph = (struct icmphdr *)((*pskb)->nh.raw + ((*pskb)->nh.iph->ihl * 4));

				if ((unsigned char *) &icmph[1] > (*pskb)->tail) {
					dbg("vnet_out:%d: dropping runt ICMP packet\n", xid);
					return NF_DROP;
				}
				
				switch (icmph->type) {
				case ICMP_ECHOREPLY:
				case ICMP_ECHO:
				case ICMP_TIMESTAMP:
				case ICMP_TIMESTAMPREPLY:
				case ICMP_INFO_REQUEST:
				case ICMP_INFO_REPLY:
				case ICMP_ADDRESS:
				case ICMP_ADDRESSREPLY:
					/* Guaranteed by icmp_pkt_to_tuple() */
					assert(port == icmph->un.echo.id);
					break;
				default:
					dbg("vnet_out:%d: sending of ICMP error messages not allowed\n", xid);
					return NF_DROP;
				}
			}
		} else {
			/* Let root send anything it wants */
		}

		if (ct) {
			/* Check if the port is bound by someone else */
			key = bind_get(protocol, ip, port, NULL);
		} else {
			assert(xid == 0);
			key = NULL;
		}

		if (key && key->sk != NULL) {
			/* A new or established connection from a bound port */
			assert(ct);

			sk = key->sk;

#ifdef HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX
			/* If the bound socket is a real TCP socket, then the context that
			 * bound the port could have re-assigned an established connection
			 * socket to the sender's context. See if this is the case.
			 */
			if (protocol == IPPROTO_TCP && sk->sk_type == SOCK_STREAM && get_sk_xid(sk) != xid) {
				struct sock *tcp_sk;
				u_int32_t dst_ip = ct->tuplehash[dir].tuple.dst.ip;
				u_int16_t dst_port = get_dst_port(&ct->tuplehash[dir].tuple);

				tcp_sk = vnet_tcp_lookup(dst_ip, dst_port, ip, port, vnet_iif(*pskb));
				if (tcp_sk) {
				  if (tcp_sk->sk_state == TCP_TIME_WAIT) {
				    sock_put(tcp_sk);
				    //vnet_timewait_put(tcp_sk);
				  } else {
				    need_to_free_sk = 1;
				    sk = tcp_sk;
				    /* Remember to sock_put()! */
				  }
				}
			}
#endif

			verdict = NF_ACCEPT;

			/* Stealing connections from established sockets is not allowed */
			assert(sk);
			if (get_sk_xid(sk) != xid) {
				if (xid) {
					dbg("vnet_out:%d: %s port %u.%u.%u.%u:%u already bound by context %d\n", xid,
					    print_protocol(protocol), NIPQUAD(ip), ntohs(port), get_sk_xid(sk));
					verdict = NF_DROP;
				} else {
					/* Let root send whatever it wants but do not steal the packet or
					 * connection. Kernel sockets owned by root may send packets on
					 * behalf of bound sockets (for instance, TCP ACK in SYN_RECV or
					 * TIME_WAIT).
					 */
					xid = get_sk_xid(sk);
				}
			}

#ifdef HAVE_FUNCTIONALITY_REQUIRED_BY_DEMUX
			if (need_to_free_sk) {
			/*
			  if (sk->sk_state == TCP_TIME_WAIT)
			    vnet_timewait_put(sk);
			  else */
			  sock_put(sk);
			  need_to_free_sk = 0;
			}
#endif
			bind_put(key);

			if (verdict == NF_DROP)
				goto done;
		} else {
			/* A new or established or untrackable connection from an unbound port */

			/* Reserved ports must be bound. Usually only root is capable of
			 * CAP_NET_BIND_SERVICE.
			 */
			if (xid &&
			    (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP) &&
			    ntohs(port) < PROT_SOCK) {
				assert(ct);
				dbg("vnet_out:%d: %s port %u is reserved\n", xid,
				    print_protocol(protocol), ntohs(port));
				return NF_DROP;
			}
		}

		if (ct) {
			/* Steal the connection */
			if (get_ct_xid(ct, dir) != xid) {
				dbg("vnet_out:%d: stealing %sbound %s connection %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u from context %d\n", xid,
				    key ? "" : "un", print_protocol(protocol),
				    NIPQUAD(ip), ntohs(port),
				    NIPQUAD(ct->tuplehash[dir].tuple.dst.ip), ntohs(ct->tuplehash[dir].tuple.dst.u.all),
				    get_ct_xid(ct, dir));
				set_ct_xid(ct, dir, xid);
			}

			/* Classify traffic once per connection */
			if (ct->priority == (u_int32_t) -1) {
				/* The POSTROUTING chain should classify packets into a minor subclass
				 * (1:1000, 1:2000, etc.) with -j CLASSIFY --set-class. Set the packet
				 * MARK early so that rules can take xid into account. */
				set_skb_xid(*pskb, xid);
				(*pskb)->priority = priority;
				(void) ipt_do_table(pskb, NF_IP_POST_ROUTING, in, out, &vnet_table, NULL);
				priority = (*pskb)->priority | xid;
				dbg("vnet_out:%d: %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u class %x:%x\n", xid,
				    NIPQUAD(ip), ntohs(port),
				    NIPQUAD(ct->tuplehash[dir].tuple.dst.ip), ntohs(ct->tuplehash[dir].tuple.dst.u.all),
				    TC_H_MAJ(priority) >> 16, TC_H_MIN(priority));
				ct->priority = priority;
			} else
				priority = ct->priority;
		} else {
			assert(xid == 0);
		}

		/* Set class */
		(*pskb)->priority = priority;

		break;

	default:
		/* Huh? */
		assert(hook == NF_IP_LOCAL_IN || hook == NF_IP_LOCAL_OUT);
		break;
	}

	/* Mark packet */
	set_skb_xid(*pskb, xid);

#ifdef VNET_DEBUG
	if (vnet_verbose >= 3) {
		if (ct)
			print_conntrack(ct, ctinfo, hook);
		if (vnet_verbose >= 4)
			print_packet(*pskb);
	}
#endif

 get_verdict:
	verdict = ipt_do_table(pskb, hook, in, out, &vnet_table, NULL);

	/* Pass to network taps */
	if (verdict == NF_ACCEPT)
		verdict = packet_hook(*pskb, hook);

 done:
	return verdict;
}

static struct nf_hook_ops vnet_ops[] = {
	{
		.hook		= vnet_hook,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		.owner		= THIS_MODULE,
#endif
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_LAST,
	},
	{
		.hook		= vnet_hook,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		.owner		= THIS_MODULE,
#endif
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_LAST,
	},
};

/* Exported by net/ipv4/af_inet.c */
extern struct net_proto_family inet_family_ops;
extern struct proto_ops inet_stream_ops;
extern struct proto_ops inet_dgram_ops;
extern struct proto_ops inet_sockraw_ops;
extern int inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
extern int inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			       int addr_len, int flags);
extern int inet_listen(struct socket *sock, int backlog);
extern int inet_dgram_connect(struct socket *sock, struct sockaddr * uaddr,
			      int addr_len, int flags);
extern int inet_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
			size_t size);
extern int inet_release(struct socket *sock);

/* Exported by net/ipv4/tcp_ipv4.c */
extern struct proto tcp_prot;
extern int tcp_port_rover;
extern int sysctl_local_port_range[2];

/* Exported by net/ipv4/udp.c */
extern struct proto udp_prot;
extern int udp_port_rover;

/* Functions that are not exported */
static int (*inet_create)(struct socket *sock, int protocol);
static ssize_t (*inet_sendpage)(struct socket *sock, struct page *page, int offset, size_t size, int flags);
static void (*tcp_v4_hash)(struct sock *sk);
static void (*tcp_v4_unhash)(struct sock *sk);
static void (*udp_v4_hash)(struct sock *sk);
static void (*udp_v4_unhash)(struct sock *sk);

static int
vnet_inet_create(struct socket *sock, int protocol)
{
	int ret;

	if (sock->type == SOCK_RAW) {
		/* Temporarily give CAP_NET_RAW to root VServer accounts */
		if (current->euid)
			return -EPERM;
		cap_raise(current->cap_effective, CAP_NET_RAW);
	}
	ret = inet_create(sock, protocol);
	if (sock->type == SOCK_RAW)
		cap_lower(current->cap_effective, CAP_NET_RAW);
	if (ret)
		return ret;

	if (sock->type == SOCK_RAW) {
		struct sock *sk = sock->sk;
		struct inet_opt *inet = inet_sk(sk);
		/* Usually redundant and unused */
		assert(inet->sport == htons(inet->num));
		/* So we can track double raw binds */
		inet->sport = 0;
	}

	return ret;
}

/* Make sure our bind table gets updated whenever the stack decides to
 * unhash or rehash a socket.
 */
static void
vnet_inet_unhash(struct sock *sk)
{
	struct inet_opt *inet = inet_sk(sk);
	struct bind_key *key;

	key = bind_get(sk->sk_protocol, inet->saddr, inet->sport, sk);
	if (key) {
		dbg("vnet_inet_unhash:%d: released %s port %u.%u.%u.%u:%u\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), ntohs(inet->sport));
		bind_del(key);
		bind_put(key);
	}

	if (sk->sk_protocol == IPPROTO_TCP)
		tcp_v4_unhash(sk);
	else if (sk->sk_protocol == IPPROTO_UDP)
		udp_v4_unhash(sk);
}

static void
vnet_inet_hash(struct sock *sk)
{
	struct inet_opt *inet = inet_sk(sk);

	if (bind_add(sk->sk_protocol, inet->saddr, inet->sport, sk) == 0) {
		dbg("vnet_inet_hash:%d: bound %s port %u.%u.%u.%u:%u\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), ntohs(inet->sport));
	}

	if (sk->sk_protocol == IPPROTO_TCP)
		tcp_v4_hash(sk);
	else if (sk->sk_protocol == IPPROTO_UDP)
		udp_v4_hash(sk);
}

/* Port reservation */
static int
vnet_inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct inet_opt *inet = inet_sk(sk);
	struct sockaddr_in *sin = (struct sockaddr_in *) uaddr;
	struct bind_key *key;
	int ret;

	/* Bind socket */
	if ((ret = inet_bind(sock, uaddr, addr_len)))
		return ret;

	lock_sock(sk);

	/* Backward compatibility with safe raw sockets */
	if (sock->type == SOCK_RAW) {
		/* Runt sockaddr */
		if (addr_len < sizeof(struct sockaddr_in))
			ret = -EINVAL;
		/* Non-local bind */
		else if (sin->sin_addr.s_addr != __constant_htonl(INADDR_ANY) && inet_addr_type(sin->sin_addr.s_addr) != RTN_LOCAL)
			ret = -EINVAL;
		/* Unspecified port */
		else if (!sin->sin_port)
			ret = -EINVAL;
		/* Reserved port */
		else if ((sk->sk_protocol == IPPROTO_TCP || sk->sk_protocol == IPPROTO_UDP) &&
			 ntohs(sin->sin_port) < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
			ret = -EACCES;
		/* Double bind */
		else if (inet->sport)
			ret = -EINVAL;
		if (ret)
			goto done;
		inet->saddr = sin->sin_addr.s_addr;
		inet->sport = sin->sin_port;
	}

	key = bind_get(sk->sk_protocol, inet->saddr, inet->sport, NULL);
	if (key) {
		/*
		 * If we are root or own the already bound socket, and
		 * SO_REUSEADDR has been set on both.
		 */
		if ((get_sk_xid(sk) == 0 || get_sk_xid(sk) == get_sk_xid(key->sk)) &&
		    key->sk->sk_reuse && sk->sk_reuse) {
			if (key->ip == __constant_htonl(INADDR_ANY)) {
				/* Keep the current bind key */
				bind_put(key);
				goto done;
			} else if (inet->saddr == __constant_htonl(INADDR_ANY)) {
				/* Consider the port to be bound to this socket now */
				bind_del(key);
			}
		}
		bind_put(key);
	}

	if ((ret = bind_add(sk->sk_protocol, inet->saddr, inet->sport, sk)) == 0) {
		dbg("vnet_inet_bind:%d: bound %s port %u.%u.%u.%u:%u\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), ntohs(inet->sport));
	}

 done:
	release_sock(sk);
	return ret;
}

/* Override TCP and UDP port rovers since they do not know about raw
 * socket binds.
 */
static int
vnet_autobind(struct sock *sk)
{
	int (*get_port)(struct sock *, unsigned short);
	int low = sysctl_local_port_range[0];
	int high = sysctl_local_port_range[1];
	int remaining = (high - low) + 1;
	int port;
	struct inet_opt *inet = inet_sk(sk);
	struct bind_key *key;

	/* Must be locked */
	assert(sock_owned_by_user(sk));

	/* Already bound to a port */
	if (inet->num)
		return 0;

	if (sk->sk_protocol == IPPROTO_TCP) {
		get_port = tcp_prot.get_port;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
		/* Approximate the tcp_v4_get_port() strategy */
		port = tcp_port_rover + 1;
#else
		/* Approximate the inet_csk_get_port() strategy */
		port = net_random() % (high - low) + low;
#endif
	} else if (sk->sk_protocol == IPPROTO_UDP) {
		get_port = udp_prot.get_port;
		port = udp_port_rover;
	} else if (sk->sk_prot->get_port) {
		err("vnet_get_port:%d: %s unhandled\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol));
		if (sk->sk_prot->get_port(sk, 0))
			return -EAGAIN;
		inet->sport = htons(inet->num);
		return 0;
	} else {
		return 0;
	}

	dbg("vnet_autobind:%d: roving %s port range %u.%u.%u.%u:%u-%u\n", get_sk_xid(sk),
	    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), low, high);

	/* Find a free port by linear search. Note that the standard
	 * udp_v4_get_port() function attempts to pick a port that
	 * keeps its hash tables balanced. If the UDP hash table keeps
	 * getting bombed, we should try implementing this strategy
	 * here.
	 */
	do {
		if (port < low || port > high)
			port = low;

		/* XXX We could probably try something more clever
		 * like checking to see if the bound socket is a
		 * regular TCP socket owned by the same context (or we
		 * are root) and, if so, letting tcp_v4_get_port()
		 * apply its fast reuse logic to determine if the port
		 * can be reused.
		 */
		if (bind_add(sk->sk_protocol, inet->saddr, htons(port), sk)) {
			dbg("vnet_get_port:%d: %s port %u.%u.%u.%u:%u already bound\n", get_sk_xid(sk),
			    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), port);
			goto next;
		}

		if (get_port(sk, port)) {
			/* Can happen if we are unloaded when there are active sockets */
			dbg("vnet_get_port:%d: failed to hash unbound %s port %u.%u.%u.%u:%u\n", get_sk_xid(sk),
			    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), port);
			key = bind_get(sk->sk_protocol, inet->saddr, htons(port), sk);
			assert(key);
			bind_del(key);
			bind_put(key);
		} else {
			assert(port == inet->num);
			inet->sport = htons(inet->num);
			break;
		}
	next:
		port++;
	} while (--remaining > 0);

	if (sk->sk_protocol == IPPROTO_UDP)
		udp_port_rover = port;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
	else if (sk->sk_protocol == IPPROTO_TCP)
		tcp_port_rover = port;
#endif

	if (remaining <= 0) {
		err("vnet_get_port:%d: exhausted local %s port range %u.%u.%u.%u:%u-%u\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), low, high);
		return -EAGAIN;
	} else {
		dbg("vnet_get_port:%d: autobound %s port %u.%u.%u.%u:%u\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), port);
		return 0;
	}
}

static int
vnet_inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			 int addr_len, int flags)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);

	/* Duplicates checks in inet_stream_connect() */
	if (uaddr->sa_family != AF_UNSPEC &&
	    sock->state == SS_UNCONNECTED &&
	    sk->sk_state == TCP_CLOSE) {
		/* We may need to bind the socket. */
		if (!inet_sk(sk)->num && vnet_autobind(sk)) {
			release_sock(sk);
			return -EAGAIN;
		}
	}

	release_sock(sk);

	return inet_stream_connect(sock, uaddr, addr_len, flags);
}

static int 
vnet_inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);

	/* Duplicates checks in inet_listen() */
	if (sock->type == SOCK_STREAM &&
	    sock->state == SS_UNCONNECTED &&
	    sk->sk_state == TCP_CLOSE) {
		/* We may need to bind the socket. */
		if (!inet_sk(sk)->num && vnet_autobind(sk)) {
			release_sock(sk);
			return -EAGAIN;
		}
	}

	release_sock(sk);

	return inet_listen(sock, backlog);
}

static int
vnet_inet_dgram_connect(struct socket *sock, struct sockaddr * uaddr,
			int addr_len, int flags)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);

	/* Duplicates checks in inet_dgram_connect() */
	if (uaddr->sa_family != AF_UNSPEC) {
		/* We may need to bind the socket. */
		if (!inet_sk(sk)->num && vnet_autobind(sk)) {
			release_sock(sk);
			return -EAGAIN;
		}
	}

	release_sock(sk);

	return inet_dgram_connect(sock, uaddr, addr_len, flags);
}

static int
vnet_inet_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
		  size_t size)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);

	/* We may need to bind the socket. */
	if (!inet_sk(sk)->num && vnet_autobind(sk)) {
		release_sock(sk);
		return -EAGAIN;
	}

	release_sock(sk);

	return inet_sendmsg(iocb, sock, msg, size);
}

static ssize_t
vnet_inet_sendpage(struct socket *sock, struct page *page, int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);

	/* We may need to bind the socket. */
	if (!inet_sk(sk)->num && vnet_autobind(sk)) {
		release_sock(sk);
		return -EAGAIN;
	}

	release_sock(sk);

	return inet_sendpage(sock, page, offset, size, flags);
}

static int
vnet_inet_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct inet_opt *inet = inet_sk(sk);
	struct bind_key *key;

	/* Partial socket created by accept() */
	if (!sk)
		goto done;

	lock_sock(sk);

	key = bind_get(sk->sk_protocol, inet->saddr, inet->sport, sk);
	if (key) {
		dbg("vnet_inet_release:%d: released %s port %u.%u.%u.%u:%u\n", get_sk_xid(sk),
		    print_protocol(sk->sk_protocol), NIPQUAD(inet->saddr), ntohs(inet->sport));
		bind_del(key);
		bind_put(key);
	}

	release_sock(sk);

 done:
	return inet_release(sock);
}

/* Sanity check */
#define override_op(op, from, to) do { assert((op) == (from)); (op) = (to); } while (0)

static int __init
vnet_init(void)
{
	int ret;

	/* Initialize bind table */
	ret = bind_init();
	if (ret < 0)
		return ret;

	/* Register /proc entries */
	ret = proc_init();
	if (ret < 0)
		goto cleanup_bind;

	/* Register dummy netdevice */
	ret = packet_init();
	if (ret < 0)
		goto cleanup_proc;

	/* Register tap netdevice */
	ret = tun_init();
	if (ret < 0)
		goto cleanup_packet;

	/* Get pointers to unexported functions */
	inet_create = inet_family_ops.create;
	inet_sendpage = inet_dgram_ops.sendpage;
	tcp_v4_hash = tcp_prot.hash;
	tcp_v4_unhash = tcp_prot.unhash;
	udp_v4_hash = udp_prot.hash;
	udp_v4_unhash = udp_prot.unhash;

	/* Override PF_INET socket operations */
	override_op(inet_family_ops.create, inet_create, vnet_inet_create);
	override_op(inet_stream_ops.bind, inet_bind, vnet_inet_bind);
	override_op(inet_stream_ops.connect, inet_stream_connect, vnet_inet_stream_connect);
	override_op(inet_stream_ops.listen, inet_listen, vnet_inet_listen);
	override_op(inet_stream_ops.sendmsg, inet_sendmsg, vnet_inet_sendmsg);
	override_op(inet_stream_ops.release, inet_release, vnet_inet_release);
	override_op(inet_dgram_ops.bind, inet_bind, vnet_inet_bind);
	override_op(inet_dgram_ops.connect, inet_dgram_connect, vnet_inet_dgram_connect);
	override_op(inet_dgram_ops.sendmsg, inet_sendmsg, vnet_inet_sendmsg); 
	override_op(inet_dgram_ops.sendpage, inet_sendpage, vnet_inet_sendpage);
	override_op(inet_dgram_ops.release, inet_release, vnet_inet_release);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	override_op(inet_sockraw_ops.bind, inet_bind, vnet_inet_bind);
	override_op(inet_sockraw_ops.connect, inet_dgram_connect, vnet_inet_dgram_connect);
	override_op(inet_sockraw_ops.sendmsg, inet_sendmsg, vnet_inet_sendmsg);
	override_op(inet_sockraw_ops.sendpage, inet_sendpage, vnet_inet_sendpage); 
	override_op(inet_sockraw_ops.release, inet_release, vnet_inet_release);
#endif
	override_op(tcp_prot.hash, tcp_v4_hash, vnet_inet_hash);
	override_op(tcp_prot.unhash, tcp_v4_unhash, vnet_inet_unhash);
	override_op(udp_prot.hash, udp_v4_hash, vnet_inet_hash);
	override_op(udp_prot.unhash, udp_v4_unhash, vnet_inet_unhash);

	/* Register table */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
	ret = ipt_register_table(&vnet_table, &initial_table.repl);
#else
	ret = ipt_register_table(&vnet_table);
#endif
	if (ret < 0)
		goto cleanup_override;

	/* Register hooks */
	ret = nf_register_hook(&vnet_ops[0]);
	if (ret < 0)
		goto cleanup_table;

	ret = nf_register_hook(&vnet_ops[1]);
	if (ret < 0)
		goto cleanup_hook0;

	/* Enables any runtime kernel support for VNET */
	vnet_active = 1;

	/* Print banner */
	printk("VNET: version " VNET_VERSION " compiled on " __DATE__ " at " __TIME__ "\n");

	return ret;

 cleanup_hook0:
	nf_unregister_hook(&vnet_ops[0]);
 cleanup_table:
	ipt_unregister_table(&vnet_table);
 cleanup_override:
	inet_family_ops.create = inet_create;
	inet_stream_ops.bind = inet_bind;
	inet_stream_ops.connect = inet_stream_connect;
	inet_stream_ops.listen = inet_listen;
	inet_stream_ops.sendmsg = inet_sendmsg;
	inet_stream_ops.release = inet_release;
	inet_dgram_ops.bind = inet_bind;
	inet_dgram_ops.connect = inet_dgram_connect;
	inet_dgram_ops.sendmsg = inet_sendmsg;
	inet_dgram_ops.sendpage = inet_sendpage;
	inet_dgram_ops.release = inet_release;
	tun_cleanup();
 cleanup_packet:
	packet_cleanup();	
 cleanup_proc:
	proc_cleanup();
 cleanup_bind:
	bind_cleanup();

	return ret;
}

static void __exit
vnet_exit(void)
{
	unsigned int i;

	/* Print banner */
	printk("VNET: exiting\n");

	/* Disables any runtime kernel support for VNET */
	vnet_active = 0;

	/* Stop handling packets first */
	for (i = 0; i < sizeof(vnet_ops)/sizeof(struct nf_hook_ops); i++)
		nf_unregister_hook(&vnet_ops[i]);

	ipt_unregister_table(&vnet_table);

	/* Stop handling PF_INET socket operations */
	override_op(inet_family_ops.create, vnet_inet_create, inet_create);
	override_op(inet_stream_ops.bind, vnet_inet_bind, inet_bind);
	override_op(inet_stream_ops.connect, vnet_inet_stream_connect, inet_stream_connect);
	override_op(inet_stream_ops.listen, vnet_inet_listen, inet_listen);
	override_op(inet_stream_ops.sendmsg, vnet_inet_sendmsg, inet_sendmsg);
	override_op(inet_stream_ops.release, vnet_inet_release, inet_release);
	override_op(inet_dgram_ops.bind, vnet_inet_bind, inet_bind);
	override_op(inet_dgram_ops.connect, vnet_inet_dgram_connect, inet_dgram_connect);
	override_op(inet_dgram_ops.sendmsg, vnet_inet_sendmsg, inet_sendmsg); 
	override_op(inet_dgram_ops.sendpage, vnet_inet_sendpage, inet_sendpage);
	override_op(inet_dgram_ops.release, vnet_inet_release, inet_release);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	override_op(inet_sockraw_ops.bind, vnet_inet_bind, inet_bind);
	override_op(inet_sockraw_ops.connect, vnet_inet_dgram_connect, inet_dgram_connect);
	override_op(inet_sockraw_ops.sendmsg, vnet_inet_sendmsg, inet_sendmsg);
	override_op(inet_sockraw_ops.sendpage, vnet_inet_sendpage, inet_sendpage); 
	override_op(inet_sockraw_ops.release, vnet_inet_release, inet_release);
#endif
	override_op(tcp_prot.hash, vnet_inet_hash, tcp_v4_hash);
	override_op(tcp_prot.unhash, vnet_inet_unhash, tcp_v4_unhash);
	override_op(udp_prot.hash, vnet_inet_hash, udp_v4_hash);
	override_op(udp_prot.unhash, vnet_inet_unhash, udp_v4_unhash);

	/* Disable tap netdevice */
	tun_cleanup();

	/* Disable vnet netdevice and stop handling PF_PACKET sockets */
	packet_cleanup();

	/* Unregister /proc handlers */
 	proc_cleanup();

	/* Cleanup bind table (must be after nf_unregister_hook()) */
	bind_cleanup();
}

module_init(vnet_init);
module_exit(vnet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Huang <mlhuang@cs.princeton.edu>");
MODULE_DESCRIPTION("VServer IP isolation");
