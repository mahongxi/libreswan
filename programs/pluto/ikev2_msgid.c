/* IKEv2 Message ID tracking, for libreswan
 *
 * Copyright (C) 2019 Andrew Cagney <cagney@gnu.org>
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
 */

#include "lswlog.h"
#include "defs.h"
#include "state.h"
#include "demux.h"
#include "connections.h"
#include "ikev2_msgid.h"
#include "log.h"
#include "ikev2.h"		/* for complete_v2_state_transition() */
#include "state_db.h"		/* for ike_sa_by_serialno() */

/*
 * Logging.
 */

static const char *jam_prefix(jambuf_t *buf, struct ike_sa *ike, struct state *wip_sa)
{
	jam(buf, "Message ID: ");
	const char *who;
	if (wip_sa == NULL) {
		who = "lost";
		jam(buf, "CHILD #%lu.#LOST", ike->sa.st_serialno);
	} else if (IS_CHILD_SA(wip_sa)) {
		who = "child";
		jam(buf, "CHILD #%lu.#%lu", ike->sa.st_serialno, wip_sa->st_serialno);
	} else {
		who = "ike";
		jam(buf, "IKE #%lu", ike->sa.st_serialno);
	}
	return who;
}

static void jam_ike_window(jambuf_t *buf, const char *what,
			   const struct v2_msgid_window *old,
			   const struct v2_msgid_window *new)
{
	jam(buf, " ike.%s.sent=%jd", what, old->sent);
	if (new != NULL && old->sent != new->sent) {
		jam(buf, "->%jd", new->sent);
	}
	jam(buf, " ike.%s.recv=%jd", what, old->recv);
	if (new != NULL && old->recv != new->recv) {
		jam(buf, "->%jd", new->recv);
	}
}

static void jam_ike_windows(jambuf_t *buf,
			    const struct v2_msgid_windows *old,
			    const struct v2_msgid_windows *new)
{
	jam_ike_window(buf, "initiator", &old->initiator,
		       new != NULL ? &new->initiator : NULL);
	jam_ike_window(buf, "responder", &old->responder,
		       new != NULL ? &new->responder : NULL);
}

static void jam_wip_sa(jambuf_t *buf, const char *who,
		       const struct v2_msgid_wip *old,
		       const struct v2_msgid_wip *new)
{
	jam(buf, " %s.wip.initiator=%jd", who, old->initiator);
	if (new != NULL && old->initiator != new->initiator) {
		jam(buf, "->%jd", new->initiator);
	}
	jam(buf, " %s.wip.responder=%jd", who, old->responder);
	if (new != NULL && old->responder != new->responder) {
		jam(buf, "->%jd", new->responder);
	}
}

static void jam_v2_msgid(struct lswlog *buf,
			 struct ike_sa *ike, struct state *wip_sa,
			 const char *fmt, va_list ap)
{
	const char *who = jam_prefix(buf, ike, wip_sa);
	jam(buf, " ");
	jam_va_list(buf, fmt, ap);
	jam(buf, ";");
	jam_ike_windows(buf, &ike->sa.st_v2_msgid_windows, NULL);
	if (wip_sa != NULL) {
		jam_wip_sa(buf, who, &wip_sa->st_v2_msgid_wip, NULL);
	}
}

void dbg_v2_msgid(struct ike_sa *ike, struct state *wip_sa,
		  const char *fmt, ...)
{
	LSWDBGP(DBG_BASE, buf) {
		va_list ap;
		va_start(ap, fmt);
		jam_v2_msgid(buf, ike, wip_sa, fmt, ap);
		va_end(ap);
	}
}

void fail_v2_msgid(where_t where, struct ike_sa *ike, struct state *wip_sa,
		   const char *fmt, ...)
{
	LSWLOG_PEXPECT_WHERE(where, buf) {
		va_list ap;
		va_start(ap, fmt);
		jam_v2_msgid(buf, ike, wip_sa, fmt, ap);
		va_end(ap);
	}
}

/*
 * Dump the MSGIDs along with any changes.
 *
 * Why not just dump the one that changed in the calling function?
 * Because MSGIDs have this strange habbit of mysteriously changing
 * between calls.
 */

static void dbg_msgids_update(const char *what,
			      enum message_role message, intmax_t msgid,
			      struct ike_sa *ike, const struct v2_msgid_windows *old_windows,
			      struct state *wip_sa, const struct v2_msgid_wip *old_wip)
{
	if (DBGP(DBG_BASE)) {
		LSWLOG_DEBUG(buf) {
			const char *who = jam_prefix(buf, ike, wip_sa);
			jam(buf, " %s", what);

			switch (message) {
			case MESSAGE_REQUEST: jam(buf, " request %jd", msgid); break;
			case MESSAGE_RESPONSE: jam(buf, " response %jd", msgid); break;
			case NO_MESSAGE: break;
			default: bad_case(message);
			}

			jam_ike_windows(buf, old_windows, &ike->sa.st_v2_msgid_windows);

			if (wip_sa != NULL) {
				jam_wip_sa(buf, who, old_wip, &wip_sa->st_v2_msgid_wip);
			}
		}
	}
}

/*
 * Maintain or reset Message IDs.
 *
 * When resetting, need to fudge things up sufficient to fool
 * ikev2_update_msgid_counters(() into thinking that this is a shiny
 * new init request.
 */

static const struct v2_msgid_windows empty_v2_msgid_windows = {
	.initiator = {
		.sent = -1,
		.recv = -1,
	},
	.responder = {
		.sent = -1,
		.recv = -1,
	},
};

static const struct v2_msgid_wip empty_v2_msgid_wip = {
	.initiator = -1,
	.responder = -1,
};

void v2_msgid_init_ike(struct ike_sa *ike)
{
	struct v2_msgid_windows old_windows = ike->sa.st_v2_msgid_windows;
	ike->sa.st_v2_msgid_windows = empty_v2_msgid_windows;
	struct v2_msgid_wip old_wip = ike->sa.st_v2_msgid_wip;
	ike->sa.st_v2_msgid_wip = empty_v2_msgid_wip;
	/* pretend there's a sender */
	dbg_msgids_update("initialize-ike", NO_MESSAGE, -1,
			  ike, &old_windows,
			  &ike->sa, &old_wip);
}

void v2_msgid_init_child(struct ike_sa *ike, struct child_sa *child)
{
	child->sa.st_v2_msgid_windows = empty_v2_msgid_windows;
	struct v2_msgid_wip old_child = child->sa.st_v2_msgid_wip;
	child->sa.st_v2_msgid_wip = empty_v2_msgid_wip;
	/* pretend there's a sender */
	dbg_msgids_update("initialize-child", NO_MESSAGE, -1,
			  ike, &ike->sa.st_v2_msgid_windows, /* unchanged */
			  &child->sa, &old_child);
}

void v2_msgid_start_responder(struct ike_sa *ike, struct state *responder,
			      const struct msg_digest *md)
{
	enum message_role role = v2_msg_role(md);
	if (!pexpect(role == MESSAGE_REQUEST)) {
		return;
	}
	/* extend msgid */
	intmax_t msgid = md->hdr.isa_msgid;
	const struct v2_msgid_wip wip = responder->st_v2_msgid_wip;

	if (DBGP(DBG_BASE) &&
	    responder->st_v2_msgid_wip.responder != -1) {
		FAIL_V2_MSGID(ike, responder,
			      "responder->st_v2_msgid_wip.responder=%jd == -1",
			      responder->st_v2_msgid_wip.responder);
	}
	responder->st_v2_msgid_wip.responder = msgid;
	dbg_msgids_update("start-responder", role, msgid,
			  ike, &ike->sa.st_v2_msgid_windows,
			  responder, &wip);
}

/*
 * XXX: This is to hack around the broken code that switches from the
 * IKE SA to the CHILD SA before sending the reply.  Instead, because
 * the CHILD SA can fail, the IKE SA should be the one processing the
 * message?
 */

void v2_msgid_switch_responder(struct ike_sa *ike, struct child_sa *child,
			       const struct msg_digest *md)
{
	enum message_role role = v2_msg_role(md);
	if (!pexpect(role == MESSAGE_REQUEST)) {
		return;
	}
	intmax_t msgid = md->hdr.isa_msgid;
	/* out with the old */
	{
		const struct v2_msgid_wip wip = ike->sa.st_v2_msgid_wip;
		if (DBGP(DBG_BASE) &&
		    ike->sa.st_v2_msgid_wip.responder != msgid) {
			FAIL_V2_MSGID(ike, &child->sa,
				      "ike->sa.st_v2_msgid_wip.responder=%jd == msgid=%jd",
				      ike->sa.st_v2_msgid_wip.responder, msgid);
		}
		ike->sa.st_v2_msgid_wip.responder = -1;
		dbg_msgids_update("switch-responder-from", role, msgid,
				  ike, &ike->sa.st_v2_msgid_windows,
				  &ike->sa, &wip);
	}
	/* in with the new */
	{
		const struct v2_msgid_wip wip = child->sa.st_v2_msgid_wip;
		if (DBGP(DBG_BASE) &&
		    child->sa.st_v2_msgid_wip.responder != -1) {
			FAIL_V2_MSGID(ike, &child->sa,
				      "child->sa.st_v2_msgid_wip.responder=%jd == -1",
				      child->sa.st_v2_msgid_wip.responder);
		}
		child->sa.st_v2_msgid_wip.responder = msgid;
		dbg_msgids_update("switch-responder-to", role, msgid,
				  ike, &ike->sa.st_v2_msgid_windows,
				  &child->sa, &wip);
	}
}

void v2_msgid_switch_initiator(struct ike_sa *ike, struct child_sa *child,
			       const struct msg_digest *md)
{
	enum message_role role = v2_msg_role(md);
	if (!pexpect(role == MESSAGE_RESPONSE)) {
		return;
	}
	intmax_t msgid = md->hdr.isa_msgid;
	/* out with the old */
	{
		const struct v2_msgid_wip wip = ike->sa.st_v2_msgid_wip;
		if (DBGP(DBG_BASE) &&
		    ike->sa.st_v2_msgid_wip.initiator != msgid) {
			FAIL_V2_MSGID(ike, &child->sa,
				      "ike->sa.st_v2_msgid_wip.initiator=%jd == msgid=%jd",
				      ike->sa.st_v2_msgid_wip.initiator, msgid);
		}
		ike->sa.st_v2_msgid_wip.initiator = -1;
		dbg_msgids_update("switch-initiator-from", role, msgid,
				  ike, &ike->sa.st_v2_msgid_windows,
				  &ike->sa, &wip);
	}
	/* in with the new */
	{
		const struct v2_msgid_wip wip = child->sa.st_v2_msgid_wip;
		if (DBGP(DBG_BASE) &&
		    child->sa.st_v2_msgid_wip.initiator != -1) {
			FAIL_V2_MSGID(ike, &child->sa,
				      "child->sa.st_v2_msgid_wip.initiator=%jd == -1",
				      child->sa.st_v2_msgid_wip.initiator);
		}
		child->sa.st_v2_msgid_wip.initiator = msgid;
		dbg_msgids_update("switch-initiator-to", role, msgid,
				  ike, &ike->sa.st_v2_msgid_windows,
				  &child->sa, &wip);
	}
}

void v2_msgid_cancel_responder(struct ike_sa *ike, struct state *responder,
			       const struct msg_digest *md)
{
	enum message_role role = v2_msg_role(md);
	if (!pexpect(role == MESSAGE_REQUEST)) {
		return;
	}
	/* extend msgid */
	intmax_t msgid = md->hdr.isa_msgid;
	const struct v2_msgid_wip wip = responder->st_v2_msgid_wip;

	/*
	 * If an encrypted message is corrupt things bail before
	 * start_responder() but then STF_IGNORE tries to clear it.
	 */
	if (DBGP(DBG_BASE) &&
	    responder->st_v2_msgid_wip.responder != msgid) {
		FAIL_V2_MSGID(ike, responder,
			      "responder->st_v2_msgid_wip.responder=%jd == msgid=%jd",
			      responder->st_v2_msgid_wip.responder, msgid);
	}
	responder->st_v2_msgid_wip.responder = -1;
	dbg_msgids_update("cancel-responder", role, msgid,
			  ike, &ike->sa.st_v2_msgid_windows,
			  responder, &wip);
}

void v2_msgid_update_recv(struct ike_sa *ike, struct state *receiver,
			  struct msg_digest *md)
{
	/* extend msgid */
	intmax_t msgid = md->hdr.isa_msgid;

	/* save old value, and add shortcut to new */
	const struct v2_msgid_windows old = ike->sa.st_v2_msgid_windows;
	struct v2_msgid_windows *new = &ike->sa.st_v2_msgid_windows;

	/*
	 * If the receiver is known, save a copy of the old values.
	 *
	 * The receiver (CHILD SA) gets lost (deleted) when processing
	 * an IKE_AUTH response and authentication fails.  When this
	 * happens all that matters is that the IKE SA is updated.
	 */
	const struct v2_msgid_wip old_receiver =
		receiver != NULL ? receiver->st_v2_msgid_wip : empty_v2_msgid_wip;

	enum message_role receiving = v2_msg_role(md);

	switch (receiving) {
	case MESSAGE_REQUEST:
		/*
		 * Processing request finished.  Scrub it as wip.
		 *
		 * XXX: should this done in update_sent() since it is
		 * when sending the response that things really
		 * finish?
		 */
		if (receiver != NULL) {
			if (DBGP(DBG_BASE) &&
			    receiver->st_v2_msgid_wip.responder != msgid) {
				FAIL_V2_MSGID(ike, receiver,
					      "wip.responder=%jd == msgid=%jd",
					      receiver->st_v2_msgid_wip.responder, msgid);
			}
			receiver->st_v2_msgid_wip.responder = -1;
		} else {
			FAIL_V2_MSGID(ike, NULL, "XXX: message request receiver lost!?!");
		}
		/* last request we received */
		new->responder.recv = msgid;
		break;
	case MESSAGE_RESPONSE:
		/*
		 * Since the response has been successfully processed,
		 * clear WIP.INITIATOR.  This way duplicate
		 * responses get discarded as there is no receiving
		 * state.
		 *
		 * XXX: Unfortunately the record 'n' send code throws
		 * a spanner in the works.  It calls update_send()
		 * before update_recv() breaking the assumption that
		 * WIP.INITIATOR is the old MSGID.
		 */
		if (receiver != NULL) {
			if (old_receiver.initiator > msgid) {
				/*
				 * Hack around record 'n' send calling
				 * update_sent() (setting
				 * WIP.INITIATOR to the next request)
				 * midway through processing.
				 *
				 * Getting rid of record 'n' send will
				 * fix this hack.
				 */
				dbg_v2_msgid(ike, receiver, "XXX: receiver.wip.initiator %jd != receiver.msgid %jd - suspect record'n'called update_sent() before update_recv()",
					     old_receiver.initiator, msgid);
			} else {
				if (DBGP(DBG_BASE) && old_receiver.initiator != msgid) {
					FAIL_V2_MSGID(ike, receiver,
						      "receiver.wip.initiator=%jd == receiver.msgid=%jd",
						      old_receiver.initiator, msgid);
				}
				receiver->st_v2_msgid_wip.initiator = -1;
			}
			/* this is what matters */
			pexpect(receiver->st_v2_msgid_wip.initiator != msgid);
		} else {
			/*
			 * For instance, the IKE_AUTH response is
			 * rejected and the child (which was the
			 * receiver) is deleted before this code is
			 * called.
			 *
			 * XXX: if the IKE SA is made the receiver
			 * this problem goes away.
			 */
			dbg("Message ID: IKE #%lu XXX: message response receiver lost; probably a deleted child",
			    ike->sa.st_serialno);
		}
		/* last response we received */
		new->initiator.recv = msgid;
		break;
	case NO_MESSAGE:
		dbg("Message ID: IKE #%lu skipping update_recv as MD is fake",
		    ike->sa.st_serialno);
		return;
	default:
		bad_case(receiving);
	}

	dbg_msgids_update("update-received", receiving, msgid,
			  ike, &old, receiver, &old_receiver);
}

void v2_msgid_update_sent(struct ike_sa *ike, struct state *sender,
			  struct msg_digest *md, enum message_role sending)
{
	struct v2_msgid_windows old = ike->sa.st_v2_msgid_windows;
	struct v2_msgid_windows *new = &ike->sa.st_v2_msgid_windows;
	struct v2_msgid_wip old_sender = sender->st_v2_msgid_wip;
	intmax_t msgid;
	switch (sending) {
	case MESSAGE_REQUEST:
		/*
		 * pluto is initiating a new exchange.
		 *
		 * Use the next Message ID (which should be what was
		 * used by the code emitting the message request)
		 */
		msgid = new->initiator.sent + 1;
		sender->st_v2_msgid_wip.initiator = new->initiator.sent = msgid;
#if 0
		/*
		 * XXX: The record 'n' send code calls update_send()
		 * before update_recv() breaking WIP.INITIATOR's
		 * expected sequence OLD-MSGID -> -1 -> NEW-MSGID.
		 */
		if (DBGP(DBG_BASE) && old_sender.initiator != -1) {
			FAIL_V2_MSGID(ike, sender,
				      "sender.wip.initiator=%jd == -1",
				      old_sender.initiator);
		}
#else
		if (old_sender.initiator != -1) {
			dbg_v2_msgid(ike, sender, "XXX: expecting sender.wip.initiator %jd == -1 - suspect record'n'send out-of-order?)",
				     old_sender.initiator);
		}
#endif
		break;
	case MESSAGE_RESPONSE:
		/*
		 * pluto is responding to MD.
		 *
		 * Since this is a response, the MD's Message ID
		 * trumps what ever is in responder.sent.  This way,
		 * when messages are lost, the counter jumps forward
		 * to the most recent received.
		 */
		passert(md != NULL);
		pexpect(v2_msg_role(md) == MESSAGE_REQUEST);
		/* extend isa_msgid */
		msgid = md->hdr.isa_msgid;
		new->responder.sent = msgid;
		break;
	case NO_MESSAGE:
		dbg_v2_msgid(ike, sender, "skipping update_send as nothing to send");
		return;
	default:
		bad_case(sending);
	}

	dbg_msgids_update("update-sent", sending, msgid,
			  ike, &old, sender, &old_sender);
}

void v2_msgid_free(struct state *st)
{
	/* find the end; small list? */
	struct v2_msgid_pending **pp = &st->st_v2_msgid_windows.initiator.pending;
	while (*pp != NULL) {
		struct v2_msgid_pending *tbd = *pp;
		*pp = tbd->next;
		pfree(tbd);
	}
}

void v2_msgid_queue_initiator(struct ike_sa *ike, struct state *st,
			      v2_msgid_pending_cb *callback)
{
	struct v2_msgid_window *initiator = &ike->sa.st_v2_msgid_windows.initiator;
	/*
	 * Always append the task.
	 */
	delete_event(st);
	event_schedule_s(EVENT_SA_REPLACE, MAXIMUM_RESPONDER_WAIT, st);
	/* find the end; small list? */
	struct v2_msgid_pending **pp = &initiator->pending;
	while (*pp != NULL)
		pp = &(*pp)->next;
	/* append */
	struct v2_msgid_pending new =  {
		.st_serialno = st->st_serialno,
		.cb = callback,
	};
	*pp = clone_thing(new, "struct initiate_list");
	v2_msgid_schedule_next_initiator(ike);
}

static void initiate_next(struct state *st, void *context UNUSED)
{
	struct ike_sa *ike = pexpect_ike_sa(st);
	if (ike == NULL) {
		dbg("IKE SA with pending initiates disappeared");
		return;
	}
	struct v2_msgid_window *initiator = &ike->sa.st_v2_msgid_windows.initiator;
	for (intmax_t unack = (initiator->sent - initiator->recv);
	     unack < ike->sa.st_connection->ike_window && initiator->pending != NULL;
	     unack++) {
		/* make a copy of head, and drop from list */
		struct v2_msgid_pending pending = *initiator->pending;
		pfree(initiator->pending);
		initiator->pending = pending.next;
		/* find that state */
		struct state *st = state_with_serialno(pending.st_serialno);
		if (st == NULL) {
			dbg_v2_msgid(ike, NULL,
				     "can't resume CHILD SA #%lu exchange as child disappeared (unack %jd)",
				     pending.st_serialno, unack);
			continue;
		}
		dbg_v2_msgid(ike, st, "resuming SA using IKE SA (unack %jd)", unack);
		so_serial_t old_state = push_cur_state(st);
		struct msg_digest *md = unsuspend_md(st);
		complete_v2_state_transition(st, &md, pending.cb(ike, st, &md));
		release_any_md(&md);
		pop_cur_state(old_state);
	}
}

void v2_msgid_schedule_next_initiator(struct ike_sa *ike)
{
	struct v2_msgid_window *initiator = &ike->sa.st_v2_msgid_windows.initiator;
	/*
	 * If there appears to be space and there's a pending
	 * initiate, poke the IKE SA so it tries to initiate things.
	 */
	if (initiator->pending != NULL) {
		intmax_t unack = (initiator->sent - initiator->recv);
		/* if this returns NULL, thats ok; will log "LOST" */
		struct state *wip_sa = state_by_serialno(initiator->pending->st_serialno);
		if (unack < ike->sa.st_connection->ike_window) {
			dbg_v2_msgid(ike, wip_sa, "wakeing IKE SA for next initiator (unack %jd)", unack);
			schedule_callback(__func__, ike->sa.st_serialno,
					  initiate_next, NULL);
		} else {
			dbg_v2_msgid(ike, wip_sa, "next initiator blocked by outstanding response (unack %jd)", unack);
		}
	} else {
		dbg_v2_msgid(ike, &ike->sa, "no pending message initiators to schedule");
	}
}
