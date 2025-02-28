/* get-next-event loop, for libreswan
 *
 * Copyright (C) 1998-2001,2013  D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2012-2013 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2013 Florian Weimer <fweimer@redhat.com>
 * Copyright (C) 2019 Andrew Cagney
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef _SERVER_H
#define _SERVER_H

#include <sys/queue.h>
#include <event2/event.h>	/* from libevent devel */
#include <event2/event_struct.h>
#include "timer.h"
#include "err.h"
#include "ip_address.h"
#include "ip_endpoint.h"

struct state;
struct msg_digest;

extern char *pluto_vendorid;

extern int ctl_fd;                      /* file descriptor of control (whack) socket */
extern struct sockaddr_un ctl_addr;     /* address of control (whack) socket */

extern int info_fd;                     /* file descriptor of control (info) socket */
extern struct sockaddr_un info_addr;    /* address of control (info) socket */

extern err_t init_ctl_socket(void);
extern void delete_ctl_socket(void);

extern bool listening;  /* should we pay attention to IKE messages? */
extern enum ddos_mode pluto_ddos_mode; /* auto-detect or manual? */
extern enum seccomp_mode pluto_seccomp_mode;
extern unsigned int pluto_max_halfopen; /* Max allowed half-open IKE SA's before refusing */
extern unsigned int pluto_ddos_threshold; /* Max incoming IKE before activating DCOOKIES */
extern deltatime_t pluto_shunt_lifetime; /* lifetime before we cleanup bare shunts (for OE) */
extern unsigned int pluto_sock_bufsize; /* pluto IKE socket buffer */
extern bool pluto_sock_errqueue; /* Enable MSG_ERRQUEUE on IKE socket */

/* interface: a terminal point for IKE traffic, IPsec transport mode
 * and IPsec tunnels.
 * Essentially:
 * - an IP device (eg. eth1), and
 * - its partner, an ipsec device (eg. ipsec0), and
 * - their shared IP address (eg. 10.7.3.2)
 * Note: the port for IKE is always implicitly UDP/pluto_port.
 *
 * The iface is a unique IP address on a system. It may be used
 * by multiple port numbers. In general, two conns have the same
 * interface if they have the same iface_port->iface_alias.
 */
struct iface_dev {
	LIST_ENTRY(iface_dev) id_entry;
	int id_count;
	char *id_vname; /* virtual (ipsec) device name */
	char *id_rname; /* real device name */
	bool id_nic_offload;
};

struct iface_port {
	struct iface_dev   *ip_dev;
	ip_endpoint local_endpoint;	/* interface IP address:port */
	int fd;                 /* file descriptor of socket for IKE UDP messages */
	struct iface_port *next;
	bool ike_float;
	enum { IFN_ADD, IFN_KEEP, IFN_DELETE } change;
	struct pluto_event *pev;
};

extern struct iface_port  *interfaces;   /* public interfaces */
extern enum pluto_ddos_mode ddos_mode;
extern bool pluto_drop_oppo_null;

extern struct iface_port *find_iface_port_by_local_endpoint(ip_endpoint *local_endpoint);
extern bool use_interface(const char *rifn);
extern void find_ifaces(bool rm_dead);
extern void show_ifaces_status(void);
extern void free_ifaces(void);
extern void show_debug_status(void);
extern void show_fips_status(void);
extern void call_server(char *conffile);
typedef void event_callback_routine(evutil_socket_t, const short, void *);
void fire_timer_photon_torpedo(struct event **evp, event_callback_fn cb, void *arg,
			       const deltatime_t delay);
extern struct pluto_event *add_fd_read_event_handler(evutil_socket_t fd,
						     event_callback_fn cb, void *arg,
						     const char *name);
extern void delete_pluto_event(struct pluto_event **evp);
extern void link_pluto_event_list(struct pluto_event *e);
bool ev_before(struct pluto_event *pev, deltatime_t delay);
extern void set_pluto_busy(bool busy);
extern void set_whack_pluto_ddos(enum ddos_mode mode);

extern void init_server(void);
extern void free_server(void);

extern struct event_base *get_pluto_event_base(void);

/*
 * Schedule an event (with no timeout) to resume a suspended state.
 * SERIALNO (so_serial_t) is used to identify the state because the
 * state object may not be directly accessable (as happens with worker
 * threads).
 *
 * For instance: a worker thread needing to resume processing of a
 * state on the main thread once crypto has completed; by the main
 * thread when faking STF_SUSPEND by scheduling a new event.
 *
 * On callback:
 *
 * The CALLBACK must check ST's value: if it is NULL then the state
 * "disappeared"; if it is non-NULL then it is for SERIALNO.  Either
 * way the CALLBACK is responsible for releasing CONTEXT.
 *
 * MDP either points at the unsuspended contents of .st_suspended_md,
 * or NULL.  On return, if *MDP is non-NULL, then it will be released.
 *
 * XXX: There's a design flaw here - what happens if a state is
 * simultaneously processing a request and a response - there's only
 * space for one message!  Suspect what saves things is that it
 * doesn't happen in the real world.
 *
 * XXX: resume_cb should return stf_status, but doing this is a mess.
 */

typedef stf_status resume_cb(struct state *st, struct msg_digest **mdp,
			     void *context);
void schedule_resume(const char *name, so_serial_t serialno,
		     resume_cb *callback, void *context);

/*
 * Schedule a callback on the main event loop now.
 *
 * Unlike schedule_resume(), SERIALNO can be SOS_NOBODY and this
 * doesn't try to unsuspend MD.
 */

typedef void callback_cb(struct state *st, void *context);
void schedule_callback(const char *name, so_serial_t serialno,
		       callback_cb *callback, void *context);

/*
 * Create a child process using fork()
 *
 * Typically used to perform a thread unfriendly operation, such as
 * calling PAM.
 *
 * On callback:
 *
 * ST either points at the state matching SERIALNO, or NULL (SERIALNO
 * is either SOS_NOBODY or the state doesn't exist).  A CB expecting a
 * state back MUST check ST before processing.  Caller sets CUR_STATE
 * so don't play with that.
 *
 * MDP either points at the unsuspended contents of .st_suspended_md,
 * or NULL.  On return, if *MDP is non-NULL, then it will be released.
 *
 * STATUS is the child processes exit code as returned by things like
 * waitpid().
 */

typedef void pluto_fork_cb(struct state *st, struct msg_digest **mdp,
			   int status, void *context);
extern int pluto_fork(const char *name, so_serial_t serialno,
		      int op(void *context),
		      pluto_fork_cb *callback, void *context);

bool check_incoming_msg_errqueue(const struct iface_port *ifp, const char *before);
void check_outgoing_msg_errqueue(const struct iface_port *ifp, const char *before);

#endif /* _SERVER_H */
