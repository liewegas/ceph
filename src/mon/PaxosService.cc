// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#include "PaxosService.h"
#include "common/Clock.h"
#include "Monitor.h"
#include "MonitorDBStore.h"


#include "common/config.h"
#include "include/assert.h"
#include "common/Formatter.h"

#define dout_subsys ceph_subsys_paxos
#undef dout_prefix
#define dout_prefix _prefix(_dout, mon, paxos, service_name, get_last_committed())
static ostream& _prefix(std::ostream *_dout, Monitor *mon, Paxos *paxos, string service_name,
			version_t lc) {
  return *_dout << "mon." << mon->name << "@" << mon->rank
		<< "(" << mon->get_state_name()
		<< ").paxosservice(" << service_name << " " << lc << ") ";
}

bool PaxosService::dispatch(PaxosServiceMessage *m)
{
  dout(10) << "dispatch " << *m << " from " << m->get_orig_source_inst() << dendl;

  if (mon->is_shutdown()) {
    m->put();
    return true;
  }

  // make sure this message isn't forwarded from a previous election epoch
  if (m->rx_election_epoch &&
      m->rx_election_epoch < mon->get_epoch()) {
    dout(10) << " discarding forwarded message from previous election epoch "
	     << m->rx_election_epoch << " < " << mon->get_epoch() << dendl;
    m->put();
    return true;
  }

  // make sure the client is still connected.  note that a proxied
  // connection will be disconnected with a null message; don't drop
  // those.  also ignore loopback (e.g., log) messages.
  if (!m->get_connection()->is_connected() &&
      m->get_connection() != mon->con_self &&
      m->get_connection()->get_messenger() != NULL) {
    dout(10) << " discarding message from disconnected client "
	     << m->get_source_inst() << " " << *m << dendl;
    m->put();
    return true;
  }

  // make sure our map is readable and up to date
  if (!is_readable(m->version)) {
    dout(10) << " waiting for paxos -> readable (v" << m->version << ")" << dendl;
    wait_for_readable(new C_RetryMessage(this, m), m->version);
    return true;
  }

  // preprocess
  if (preprocess_query(m)) 
    return true;  // easy!

  // leader?
  if (!mon->is_leader()) {
    mon->forward_request_leader(m);
    return true;
  }
  
  // writeable?
  if (!is_writeable()) {
    dout(10) << " waiting for paxos -> writeable" << dendl;
    wait_for_writeable(new C_RetryMessage(this, m));
    return true;
  }

  // update
  if (prepare_update(m)) {
    double delay = 0.0;
    if (should_propose(delay)) {
      if (delay == 0.0) {
	propose_pending();
      } else {
	// delay a bit
	if (!proposal_timer) {
	  proposal_timer = new C_Propose(this);
	  dout(10) << " setting proposal_timer " << proposal_timer << " with delay of " << delay << dendl;
	  mon->timer.add_event_after(delay, proposal_timer);
	} else { 
	  dout(10) << " proposal_timer already set" << dendl;
	}
      }
    } else {
      dout(10) << " not proposing" << dendl;
    }
  }     
  return true;
}

void PaxosService::refresh(bool *need_bootstrap)
{
  // update cached versions
  cached_last_committed = mon->store->get(get_service_name(), last_committed_name);

  dout(10) << __func__ << dendl;

  update_from_paxos(need_bootstrap);
}


void PaxosService::scrub()
{
  dout(10) << __func__ << dendl;
}

bool PaxosService::should_propose(double& delay)
{
  // simple default policy: quick startup, then some damping.
  if (get_last_committed() <= 1)
    delay = 0.0;
  else {
    utime_t now = ceph_clock_now(g_ceph_context);
    if ((now - paxos->last_commit_time) > g_conf->paxos_propose_interval)
      delay = (double)g_conf->paxos_min_wait;
    else
      delay = (double)(g_conf->paxos_propose_interval + paxos->last_commit_time
		       - now);
  }
  return true;
}


void PaxosService::_encode_pending(MonitorDBStore::Transaction *t)
{
  encode_pending(t);
}

void PaxosService::propose_pending()
{
  dout(10) << "propose_pending" << dendl;
  assert(have_pending);
  assert(mon->is_leader());
  assert(is_active());
  if (!is_active())
    return;

  if (proposal_timer) {
    dout(10) << " canceling proposal_timer " << proposal_timer << dendl;
    mon->timer.cancel_event(proposal_timer);
    proposal_timer = NULL;
  }

  /**
   * @note The value we propose is encoded in a bufferlist, passed to 
   *	   Paxos::propose_new_value and it is obtained by calling a 
   *	   function that must be implemented by the class implementing us.
   *	   I.e., the function encode_pending will be the one responsible
   *	   to encode whatever is pending on the implementation class into a
   *	   bufferlist, so we can then propose that as a value through Paxos.
   */
  MonitorDBStore::Transaction t;
  bufferlist bl;

  _encode_pending(&t);

  have_pending = false;

  dout(30) << __func__ << " transaction dump:\n";
  JSONFormatter f(true);
  t.dump(&f);
  f.flush(*_dout);
  *_dout << dendl;

  t.encode(bl);

  // apply to paxos
  proposing = true;
  paxos->propose_new_value(bl, new C_Committed(this));
}

void PaxosService::restart()
{
  dout(10) << "restart" << dendl;
  if (proposal_timer) {
    dout(10) << " canceling proposal_timer " << proposal_timer << dendl;
    mon->timer.cancel_event(proposal_timer);
    proposal_timer = 0;
  }

  finish_contexts(g_ceph_context, waiting_for_finished_proposal, -EAGAIN);

  on_restart();
}

void PaxosService::election_finished()
{
  dout(10) << "election_finished" << dendl;

  if (proposal_timer) {
    dout(10) << " canceling proposal_timer " << proposal_timer << dendl;
    mon->timer.cancel_event(proposal_timer);
    proposal_timer = 0;
  }

  if (have_pending) {
    discard_pending();
    have_pending = false;
  }
  proposing = false;

  finish_contexts(g_ceph_context, waiting_for_finished_proposal, -EAGAIN);

  // make sure we update our state
  if (is_active())
    _active();
  else
    wait_for_active(new C_Active(this));
}

void PaxosService::_active()
{
  if (!is_active()) {
    dout(10) << "_active - not active" << dendl;
    wait_for_active(new C_Active(this));
    return;
  }
  dout(10) << "_active" << dendl;

  scrub();

  // create pending state?
  if (mon->is_leader() && is_active()) {
    dout(7) << "_active creating new pending" << dendl;
    if (!have_pending) {
      create_pending();
      have_pending = true;
    }

    if (get_last_committed() == 0) {
      // create initial state
      create_initial();
      propose_pending();
      return;
    }
  } else {
    if (!mon->is_leader()) {
      dout(7) << __func__ << " we are not the leader, hence we propose nothing!" << dendl;
    } else if (!is_active()) {
      dout(7) << __func__ << " we are not active, hence we propose nothing!" << dendl;
    }
  }

  // wake up anyone who came in while we were proposing.  note that
  // anyone waiting for the previous proposal to commit is no longer
  // on this list; it is on Paxos's.
  finish_contexts(g_ceph_context, waiting_for_finished_proposal, 0);

  // NOTE: it's possible that this will get called twice if we commit
  // an old paxos value.  Implementations should be mindful of that.
  if (is_active())
    on_active();
}


void PaxosService::shutdown()
{
  cancel_events();

  if (proposal_timer) {
    dout(10) << " canceling proposal_timer " << proposal_timer << dendl;
    mon->timer.cancel_event(proposal_timer);
    proposal_timer = 0;
  }

  finish_contexts(g_ceph_context, waiting_for_finished_proposal, -EAGAIN);
}

