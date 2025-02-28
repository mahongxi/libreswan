/* demultiplex incoming IKE messages
 *
 * Copyright (C) 1998-2002,2013-2016 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2007 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2007-2008 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2013,2017 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2015 Antony Antony <antony@phenome.org>
 * Copyright (C) 2017-2019 Andrew Cagney <cagney@gnu.org>
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
 *
 * (all the code that used to be here is now in ikev1.c)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>   /* only used for belt-and-suspenders select call */
#include <sys/poll.h>   /* only used for forensic poll call */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(IP_RECVERR) && defined(MSG_ERRQUEUE)
#  include <asm/types.h>        /* for __u8, __u32 */
#  include <linux/errqueue.h>
#  include <sys/uio.h>          /* struct iovec */
#endif


#include "sysdep.h"
#include "constants.h"
#include "lswlog.h"

#include "defs.h"
#include "id.h"
#include "x509.h"
#include "certs.h"
#include "connections.h"        /* needs id.h */
#include "state.h"
#include "packet.h"
#include "crypto.h"
#include "ike_alg.h"
#include "log.h"
#include "demux.h"      /* needs packet.h */
#include "ikev1.h"
#include "ikev2.h"
#include "ipsec_doi.h"  /* needs demux.h and state.h */
#include "timer.h"
#include "udpfromto.h"
#include "ip_sockaddr.h"
#include "ip_address.h"
#include "ip_endpoint.h"
#include "ip_info.h"
#include "pluto_stats.h"
#include "ikev2_send.h"

/*
 * read the message.
 *
 * Since we don't know its size, we read it into an overly large
 * buffer and then copy it to a new, properly sized buffer.
 */

static struct msg_digest *read_packet(const struct iface_port *ifp)
{
	int packet_len;
	/* ??? this buffer seems *way* too big */
	uint8_t bigbuffer[MAX_INPUT_UDP_SIZE];

	uint8_t *_buffer = bigbuffer;
	ip_sockaddr from;
	zero(&from);
	socklen_t from_len = sizeof(from);
#if defined(HAVE_UDPFROMTO)
	ip_sockaddr to;
	zero(&to);
	socklen_t to_len   = sizeof(to);
#endif

#if defined(HAVE_UDPFROMTO)
	packet_len = recvfromto(ifp->fd, bigbuffer,
				sizeof(bigbuffer), /*flags*/ 0,
				&from.sa, &from_len,
				&to.sa, &to_len);
#else
	packet_len = recvfrom(ifp->fd, bigbuffer,
			      sizeof(bigbuffer), /*flags*/ 0,
			      &from.sa, &from_len);
#endif
	int packet_errno = errno; /* save!!! */

	/* we do not do anything with *to* addresses yet... we will */

	/*
	 * Try to decode the from address and then use it to report
	 * any actual I/O error.  As a special case, when sockaddr is
	 * empty, generate custom error messages (why? the text isn't
	 * the best).
	 */
	ip_endpoint sender;
	const char *from_ugh = sockaddr_to_endpoint(&from, from_len, &sender);
	if (packet_len == -1) {
		if (from_len == sizeof(from) &&
		    all_zero((const void *)&from, sizeof(from))) {
			if (packet_errno == ECONNREFUSED) {
				/*
				 * Tone down scary message for vague event: We
				 * get "connection refused" in response to
				 * some datagram we sent, but we cannot tell
				 * which one.
				 */
				plog_global("some IKE message we sent has been rejected with ECONNREFUSED (kernel supplied no details)");
			} else {
				plog_global("recvfrom on %s failed; Pluto cannot decode source sockaddr in rejection: undisclosed "PRI_ERRNO,
					    ifp->ip_dev->id_rname, pri_errno(packet_errno));
			}
		} else if (from_ugh != NULL) {
			plog_global("recvfrom on %s failed; Pluto cannot decode source sockaddr in rejection: %s "PRI_ERRNO,
				    ifp->ip_dev->id_rname, from_ugh, pri_errno(packet_errno));
		} else {
			plog_from(&sender, "recvfrom on %s failed "PRI_ERRNO,
				  ifp->ip_dev->id_rname, pri_errno(packet_errno));
		}
		return NULL;
	} else if (from_ugh != NULL) {
		plog_from(&sender, "recvfrom on %s returned malformed source sockaddr: %s",
			  ifp->ip_dev->id_rname, from_ugh);
		return NULL;
	}

	if (ifp->ike_float) {
		uint32_t non_esp;

		if (packet_len < (int)sizeof(uint32_t)) {
			plog_from(&sender, "too small packet (%d)",
				  packet_len);
			return NULL;
		}
		memcpy(&non_esp, _buffer, sizeof(uint32_t));
		if (non_esp != 0) {
			plog_from(&sender, "has no Non-ESP marker");
			return NULL;
		}
		_buffer += sizeof(uint32_t);
		packet_len -= sizeof(uint32_t);
	}

	/* We think that in 2013 Feb, Apple iOS Racoon
	 * sometimes generates an extra useless buggy confusing
	 * Non ESP Marker
	 */
	{
		static const uint8_t non_ESP_marker[NON_ESP_MARKER_SIZE] =
			{ 0x00, };
		if (ifp->ike_float &&
		    packet_len >= NON_ESP_MARKER_SIZE &&
		    memeq(_buffer, non_ESP_marker,
			   NON_ESP_MARKER_SIZE)) {
			plog_from(&sender, "mangled with potential spurious non-esp marker");
			return NULL;
		}
	}

	if (packet_len == 1 && _buffer[0] == 0xff) {
		/**
		 * NAT-T Keep-alive packets should be discared by kernel ESPinUDP
		 * layer. But bogus keep-alive packets (sent with a non-esp marker)
		 * can reach this point. Complain and discard them.
		 * Possibly too if the NAT mapping vanished on the initiator NAT gw ?
		 */
		endpoint_buf eb;
		dbg("NAT-T keep-alive (bogus ?) should not reach this point. Ignored. Sender: %s",
		    str_endpoint(&sender, &eb)); /* sensitive? */
		return NULL;
	}

	/*
	 * Clone actual message contents and set up md->packet_pbs to
	 * describe it.
	 */
	struct msg_digest *md = alloc_md("msg_digest in read_packet");
	md->iface = ifp;
	md->sender = sender;

	init_pbs(&md->packet_pbs
		 , clone_bytes(_buffer, packet_len,
			       "message buffer in read_packet()")
		 , packet_len, "packet");

	endpoint_buf eb;
	endpoint_buf b2;
	dbg("*received %d bytes from %s on %s (%s)",
	    (int) pbs_room(&md->packet_pbs),
	    str_endpoint(&sender, &eb),
	    ifp->ip_dev->id_rname,
	    str_endpoint(&ifp->local_endpoint, &b2));

	if (DBGP(DBG_RAW)) {
		DBG_dump(NULL, md->packet_pbs.start, pbs_room(&md->packet_pbs));
	}

	pstats_ike_in_bytes += pbs_room(&md->packet_pbs);

	return md;
}

/*
 * process an input packet, possibly generating a reply.
 *
 * If all goes well, this routine eventually calls a state-specific
 * transition function.
 *
 * This routine will not release_any_md(mdp).  It is expected that its
 * caller will do this.  In fact, it will zap *mdp to NULL if it
 * thinks **mdp should not be freed.  So the caller should be prepared
 * for *mdp being set to NULL.
 */

void process_packet(struct msg_digest **mdp)
{
	struct msg_digest *md = *mdp;

	if (!in_struct(&md->hdr, &isakmp_hdr_desc, &md->packet_pbs,
		       &md->message_pbs)) {
		/*
		 * The packet was very badly mangled. We can't be sure
		 * of any content - not even to look for major version
		 * number!  So we'll just drop it.
		 */
		plog_md(md, "received packet with mangled IKE header - dropped");
		return;
	}

	if (md->packet_pbs.roof > md->message_pbs.roof) {
		/* Some (old?) versions of the Cisco VPN client send an additional
		 * 16 bytes of zero bytes - Complain but accept it
		 */
		if (DBGP(DBG_CONTROL)) {
			DBG_log("size (%u) in received packet is larger than the size specified in ISAKMP HDR (%u) - ignoring extraneous bytes",
				(unsigned) pbs_room(&md->packet_pbs),
				md->hdr.isa_length);
			DBG_dump("extraneous bytes:", md->message_pbs.roof,
				md->packet_pbs.roof - md->message_pbs.roof);
		}
	}

	unsigned vmaj = md->hdr.isa_version >> ISA_MAJ_SHIFT;
	unsigned vmin = md->hdr.isa_version & ISA_MIN_MASK;

	switch (vmaj) {
	case 0:
		/*
		 * IKEv2 doesn't say what to do with low versions,
		 * just drop them.
		 */
		plog_md(md, "ignoring packet with IKE major version '%d'", vmaj);
		return;

	case ISAKMP_MAJOR_VERSION: /* IKEv1 */
		if (vmin > ISAKMP_MINOR_VERSION) {
			/* RFC2408 3.1 ISAKMP Header Format:
			 *
			 * Minor Version (4 bits) - indicates the minor
			 * version of the ISAKMP protocol in use.
			 * Implementations based on this version of the
			 * ISAKMP Internet-Draft MUST set the Minor
			 * Version to 0.  Implementations based on
			 * previous versions of ISAKMP Internet- Drafts
			 * MUST set the Minor Version to 1.
			 * Implementations SHOULD never accept packets
			 * with a minor version number larger than its
			 * own, given the major version numbers are
			 * identical.
			 */
			plog_md(md, "ignoring packet with IKEv1 minor version number %d greater than %d", vmin, ISAKMP_MINOR_VERSION);
			send_notification_from_md(md, INVALID_MINOR_VERSION);
			return;
		}
		dbg(" processing version=%u.%u packet with exchange type=%s (%d)",
		    vmaj, vmin,
		    enum_name(&exchange_names_ikev1orv2, md->hdr.isa_xchg),
		    md->hdr.isa_xchg);
		process_v1_packet(mdp);
		/* our caller will release_any_md(mdp) */
		break;

	case IKEv2_MAJOR_VERSION: /* IKEv2 */
		if (vmin != IKEv2_MINOR_VERSION) {
			/* Unlike IKEv1, for IKEv2 we are supposed to try to
			 * continue on unknown minors
			 */
			plog_md(md, "Ignoring unknown IKEv2 minor version number %d", vmin);
		}
		dbg(" processing version=%u.%u packet with exchange type=%s (%d)",
		    vmaj, vmin,
		    enum_name(&exchange_names_ikev1orv2, md->hdr.isa_xchg),
		    md->hdr.isa_xchg);
		ikev2_process_packet(mdp);
		/* our caller will release_any_md(mdp) */
		break;

	default:
		plog_md(md, "message contains unsupported IKE major version '%d'", vmaj);
		/*
		 * According to 1.5.  Informational Messages outside
		 * of an IKE SA, [...] the message is always sent
		 * without cryptographic protection (outside of an IKE
		 * SA), and includes either an INVALID_IKE_SPI or an
		 * INVALID_MAJOR_VERSION notification (with no
		 * notification data).  The message is a response
		 * message, and thus it is sent to the IP address and
		 * port from whence it came with the same IKE SPIs and
		 * the Message ID and Exchange Type are copied from
		 * the request.  The Response flag is set to 1, and
		 * the version flags are set in the normal fashion.
		 *
		 * XXX: But this this doesn't specify the I
		 * (Initiator) flag.  Normally the MD's I flag can be
		 * flipped.  But does IKEv++ even have an I
		 * (Initiator) flag?  Presumably it's an initial
		 * response so the flag should be clear.
		 *
		 * XXX: According to 2.5. Version Numbers and Forward
		 * Compatibility, it "SHOULD send an unauthenticated
		 * Notify message of type INVALID_MAJOR_VERSION
		 * containing the highest (closest) version number it
		 * supports".  Does the INVALID_MAJOR_VERSION
		 * notification contain the version or is it implied
		 * by the message header.  Presumably the latter -
		 * nothing describes the Notification Data for this
		 * notification.
		 *
		 * XXX: According to 3.1. The IKE Header,
		 * implementations "MUST reject or ignore messages
		 * containing a version number greater than 2 with an
		 * INVALID_MAJOR_VERSION".  Does this mean reject
		 * IKEv++ messages that contain INVALID_MAJOR_VERSION,
		 * or reject IKEv++ messages by responding with an
		 * INVALID_MAJOR_VERSION.  Presumably the latter.
		 */
		send_v2N_response_from_md(md, v2N_INVALID_MAJOR_VERSION,
					  NULL/*no data*/);
		return;
	}
}

/*
 * Deal with state changes.
 */
static void process_md(struct msg_digest **mdp)
{
	ip_address old_from = push_cur_from((*mdp)->sender);
	process_packet(mdp);
	pop_cur_from(old_from);
	release_any_md(mdp);
	reset_cur_state();
	reset_cur_connection();
}

/* wrapper for read_packet and process_packet
 *
 * The main purpose of this wrapper is to factor out teardown code
 * from the many return points in process_packet.  This amounts to
 * releasing the msg_digest and resetting global variables.
 *
 * When processing of a packet is suspended (STF_SUSPEND),
 * process_packet sets md to NULL to prevent the msg_digest being freed.
 * Someone else must ensure that msg_digest is freed eventually.
 *
 * read_packet is broken out to minimize the lifetime of the
 * enormous input packet buffer, an auto.
 */

static bool impair_incoming(struct msg_digest **mdp);

void comm_handle_cb(evutil_socket_t unused_fd UNUSED,
		    const short unused_event UNUSED,
		    void *arg)
{
	const struct iface_port *ifp = arg;
	/*
	 * Even though select(2) says that there is a message, it
	 * might only be a MSG_ERRQUEUE message.  At least sometimes
	 * that leads to a hanging recvfrom.  To avoid what appears to
	 * be a kernel bug, check_msg_errqueue uses poll(2) and tells
	 * us if there is anything for us to read.
	 *
	 * This is early enough that teardown isn't required:
	 * just return on failure.
	 */
	threadtime_t errqueue_start = threadtime_start();
	bool errqueue_ok = check_incoming_msg_errqueue(ifp, "read_packet");
	threadtime_stop(&errqueue_start, SOS_NOBODY,
			"%s() calling check_incoming_msg_errqueue()", __func__);
	if (!errqueue_ok) {
		return; /* no normal message to read */
	}

	threadtime_t md_start = threadtime_start();
	struct msg_digest *md = read_packet(ifp);
	if (md != NULL) {
		md->md_inception = md_start;
		if (!impair_incoming(&md)) {
			process_md(&md);
		}
		pexpect(md == NULL);
	}
	threadtime_stop(&md_start, SOS_NOBODY,
			"%s() reading and processing packet", __func__);
	pexpect_reset_globals();
}

/*
 * Impair pluto by replaying packets.
 *
 * To make things easier, all packets received are saved, in-order, in
 * a list and then various impair operations iterate over this list.
 *
 * For instance, IKEv1 sends back-to-back packets (see XAUTH).  By
 * replaying them (and everything else) this can simulate what happens
 * when the remote starts re-transmitting them.
 */

static void process_md_clone(struct msg_digest *orig, const char *fmt, ...) PRINTF_LIKE(2);
static void process_md_clone(struct msg_digest *orig, const char *fmt, ...)
{
	/* not whack FD yet is expected to be reset! */
	pexpect_reset_globals();
	struct msg_digest *md = clone_md(orig, fmt /* good enough */);

	LSWLOG(buf) {
		lswlogs(buf, "IMPAIR: start processing ");
		va_list ap;
		va_start(ap, fmt);
		lswlogvf(buf, fmt, ap);
		va_end(ap);
		lswlogf(buf, " (%d bytes)", (int)pbs_room(&md->packet_pbs));
	}
	if (DBGP(DBG_RAW)) {
		DBG_dump(NULL, md->packet_pbs.start, pbs_room(&md->packet_pbs));
	}

	process_md(&md);

	LSWLOG(buf) {
		lswlogf(buf, "IMPAIR: stop processing ");
		va_list ap;
		va_start(ap, fmt);
		lswlogvf(buf, fmt, ap);
		va_end(ap);
	}

	pexpect(md == NULL);
	pexpect_reset_globals();
}

static unsigned long replay_count;

struct replay_entry {
	struct list_entry entry;
	struct msg_digest *md;
	unsigned long nr;
};

static void jam_replay_entry(struct lswlog *buf, const void *data)
{
	const struct replay_entry *r = data;
	jam(buf, "replay packet %lu", r == NULL ? 0L : r->nr);
}

static struct list_head replay_packets;

static const struct list_info replay_info = {
	.name = "replay list",
	.jam = jam_replay_entry,
};

static void save_any_md_for_replay(struct msg_digest **mdp)
{
	if (mdp != NULL && *mdp != NULL) {
		init_list(&replay_info, &replay_packets);
		struct replay_entry *e = alloc_thing(struct replay_entry, "replay");
		e->md = clone_md(*mdp, "copy of real message");
		e->nr = ++replay_count; /* yes; pre-increment */
		e->entry = list_entry(&replay_info, e); /* back-link */
		insert_list_entry(&replay_packets, &e->entry);
		release_any_md(mdp);
	}
}

static bool impair_incoming(struct msg_digest **mdp)
{
	if (IMPAIR(REPLAY_DUPLICATES)) {
		save_any_md_for_replay(mdp);
		/* MD is the most recent entry */
		struct replay_entry *e = NULL;
		FOR_EACH_LIST_ENTRY_NEW2OLD(&replay_packets, e) {
			process_md_clone(e->md, "original packet");
			process_md_clone(e->md, "duplicate packet");
			break;
		}
	}
	if (IMPAIR(REPLAY_FORWARD)) {
		save_any_md_for_replay(mdp);
		struct replay_entry *e = NULL;
		FOR_EACH_LIST_ENTRY_OLD2NEW(&replay_packets, e) {
			process_md_clone(e->md, "replay forward: packet %lu of %lu",
					 e->nr, replay_count);
		}
	}
	if (IMPAIR(REPLAY_BACKWARD)) {
		save_any_md_for_replay(mdp);
		struct replay_entry *e = NULL;
		FOR_EACH_LIST_ENTRY_NEW2OLD(&replay_packets, e) {
			process_md_clone(e->md, "start replay backward: packet %lu of %lu",
					 e->nr, replay_count);
		}
	}
	/* MDP NULL implies things were impaired */
	return *mdp == NULL;
}

static callback_cb handle_md_event; /* type assertion */
static void handle_md_event(struct state *st, void *context)
{
	pexpect(st == NULL);
	struct msg_digest *md = context;
	process_md(&md);
	pexpect(md == NULL);
	pexpect_reset_globals();
}

void schedule_md_event(const char *name, struct msg_digest *md)
{
	schedule_callback(name, SOS_NOBODY, handle_md_event, md);
}

enum ike_version hdr_ike_version(const struct isakmp_hdr *hdr)
{
	unsigned vmaj = hdr->isa_version >> ISA_MAJ_SHIFT;
	switch (vmaj) {
	case ISAKMP_MAJOR_VERSION: return IKEv1;
	case IKEv2_MAJOR_VERSION: return IKEv2;
	default: return 0;
	}
}

/*
 * Map the IKEv2 MSG_R bit onto the ENUM message_role.
 *
 * Several reasons:
 *
 * - makes passing a role parameter clearer, that is:
 *       foo(MESSAGE_RESPONSE)
 *   is better than:
 *       foo(true)
 *
 * - zero is 'reserved' for no MD and/or default value so won't match
 *   either of the initiator and/or responder values
 *
 * - encourages the coding style where the two cases - REQUEST and
 *   RESPONSE - are clearly labled, that is:
 *
 *       switch(role) {
 *       case MESSAGE_REQUEST: ...; break;
 *       case MESSAGE_RESPONSE: ...; break;
 *       default: bad_case(role);
 *       }
 *
 * Separate from this is IKE SA role ORIGINAL_INITIATOR or
 * ORIGINAL_RESPONDER RFC 7296 2.2.
 */
enum message_role v2_msg_role(const struct msg_digest *md)
{
	if (md == NULL) {
		return NO_MESSAGE;
	}
	if (!pexpect(hdr_ike_version(&md->hdr) == IKEv2)) {
		return NO_MESSAGE;
	}
	if (md->fake_dne) {
		return NO_MESSAGE;
	}
	/* determine the role */
	enum message_role role =
		(md->hdr.isa_flags & ISAKMP_FLAGS_v2_MSG_R) ? MESSAGE_RESPONSE : MESSAGE_REQUEST;
	return role;
}

/*
 * cisco_stringify()
 *
 * Auxiliary function for modecfg_inR1()
 * Result is allocated on heap so caller must ensure it is freed.
 */

char *cisco_stringify(pb_stream *input_pbs, const char *attr_name)
{
	char strbuf[500]; /* Cisco maximum unknown - arbitrary choice */
	jambuf_t buf = ARRAY_AS_JAMBUF(strbuf); /* let jambuf deal with overflow */
	shunk_t str = pbs_in_left_as_shunk(input_pbs);

	/*
	 * detox string
	 */
	for (const char *p = (const void *)str.ptr, *end = p + str.len;
	     p < end && *p != '\0'; p++) {
		char c = *p;
		switch (c) {
		case '\'':
			/*
			 * preserve cisco_stringify() behaviour:
			 *
			 * ' is poison to the way this string will be
			 * used in system() and hence shell.  Remove
			 * any.
			 */
			jam(&buf, "?");
			break;
		case '\n':
		case '\r':
			/*
			 * preserve sanitize_string() behaviour:
			 *
			 * exception is that all veritical space just
			 * becomes white space
			 */
			jam(&buf, " ");
			break;
		default:
			/*
			 * preserve sanitize_string() behavour:
			 *
			 * XXX: isprint() is wrong as it is affected
			 * by locale - need portable is printable
			 * ascii; is there something hiding in the
			 * x509 sources?
			 */
			if (c != '\\' && isprint(c)) {
				jam_char(&buf, c);
			} else {
				jam(&buf, "\\%03o", c);
			}
			break;
		}
	}
	if (!jambuf_ok(&buf)) {
		loglog(RC_INFORMATIONAL, "Received overlong %s: %s (truncated)", attr_name, strbuf);
	} else {
		loglog(RC_INFORMATIONAL, "Received %s: %s", attr_name, strbuf);
	}
	return clone_str(strbuf, attr_name);
}

/*
 * list all the payload types
 */
void lswlog_msg_digest(struct lswlog *buf, const struct msg_digest *md)
{
	enum ike_version ike_version = hdr_ike_version(&md->hdr);
	lswlog_enum_enum_short(buf, &exchange_type_names, ike_version,
			       md->hdr.isa_xchg);
	if (ike_version == IKEv2) {
		switch (v2_msg_role(md)) {
		case MESSAGE_REQUEST: lswlogs(buf, " request"); break;
		case MESSAGE_RESPONSE: lswlogs(buf, " response"); break;
		case NO_MESSAGE: break;
		}
	}
	const char *sep = ": ";
	const char *term = "";
	for (unsigned d = 0; d < md->digest_roof; d++) {
		const struct payload_digest *pd = &md->digest[d];
		lswlogs(buf, sep);
		if (ike_version == IKEv2 &&
		    d+1 < md->digest_roof &&
		    pd->payload_type == ISAKMP_NEXT_v2SK) {
			/*
			 * HACK to dump decrypted SK payload contents
			 * (which come after the SK payload).
			 */
			sep = "{";
			term = "}";
		} else {
			sep = ",";
		}
		lswlog_enum_enum_short(buf, &payload_type_names,
				       ike_version,
				       pd->payload_type);
	}
	lswlogs(buf, term);
}
