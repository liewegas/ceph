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

#include "SequencePaxosService.h"
#include "common/Clock.h"
#include "Monitor.h"
#include "MonitorDBStore.h"


#include "common/config.h"
#include "include/assert.h"
#include "common/Formatter.h"

#define dout_subsys ceph_subsys_paxos
#undef dout_prefix
#define dout_prefix _prefix(_dout, mon, paxos, service_name, get_first_committed(), get_last_committed())
static ostream& _prefix(std::ostream *_dout, Monitor *mon, Paxos *paxos, string service_name,
			version_t fc, version_t lc) {
  return *_dout << "mon." << mon->name << "@" << mon->rank
		<< "(" << mon->get_state_name()
		<< ").paxosservice(" << service_name << " " << fc << ".." << lc << ") ";
}

void SequencePaxosService::refresh(bool *need_bootstrap)
{
  // update cached versions
  cached_first_committed = mon->store->get(get_service_name(), first_committed_name);
  cached_last_committed = mon->store->get(get_service_name(), last_committed_name);

  dout(10) << __func__ << dendl;

  update_from_paxos(need_bootstrap);
}

void SequencePaxosService::_encode_pending(MonitorDBStore::Transaction *t)
{
  update_trim();
  if (should_stash_full())
    encode_full(t);

  if (should_trim()) {
    encode_trim(t);
  }

  encode_pending(t);
}

void SequencePaxosService::encode_trim(MonitorDBStore::Transaction *t)
{
  version_t first_committed = get_first_committed();
  version_t latest_full = get_version_latest_full();
  version_t trim_to = get_trim_to();

  dout(10) << __func__ << " " << trim_to << " (was " << first_committed << ")"
	   << ", latest full " << latest_full << dendl;

  if (first_committed >= trim_to)
    return;

  version_t trim_to_max = trim_to;
  if ((g_conf->paxos_service_trim_max > 0)
      && (trim_to - first_committed > (size_t)g_conf->paxos_service_trim_max)) {
    trim_to_max = first_committed + g_conf->paxos_service_trim_max;
  }

  dout(10) << __func__ << " trimming versions " << first_committed
           << " to " << trim_to_max << dendl;

  trim(t, first_committed, trim_to_max);
  put_first_committed(t, trim_to_max);

  if (trim_to_max == trim_to)
    set_trim_to(0);
}

void SequencePaxosService::trim(MonitorDBStore::Transaction *t,
				version_t from, version_t to)
{
  dout(10) << __func__ << " from " << from << " to " << to << dendl;
  assert(from != to);

  for (version_t v = from; v < to; ++v) {
    dout(20) << __func__ << " " << v << dendl;
    t->erase(get_service_name(), v);

    string full_key = mon->store->combine_strings("full", v);
    if (mon->store->exists(get_service_name(), full_key)) {
      dout(20) << __func__ << " " << full_key << dendl;
      t->erase(get_service_name(), full_key);
    }
  }
  if (g_conf->mon_compact_on_trim) {
    dout(20) << " compacting prefix " << get_service_name() << dendl;
    t->compact_range(get_service_name(), stringify(from - 1), stringify(to));
  }
}


void SequencePaxosService::scrub()
{
  dout(10) << __func__ << dendl;
  if (!mon->store->exists(get_service_name(), "conversion_first"))
    return;

  version_t cf = mon->store->get(get_service_name(), "conversion_first");
  version_t fc = get_first_committed();

  dout(10) << __func__ << " conversion_first " << cf
	   << " first committed " << fc << dendl;

  MonitorDBStore::Transaction t;
  if (cf < fc) {
    trim(&t, cf, fc);
  }
  t.erase(get_service_name(), "conversion_first");
  mon->store->apply_transaction(t);
}

bool SequencePaxosService::should_stash_full()
{
  version_t latest_full = get_version_latest_full();
  /* @note The first member of the condition is moot and it is here just for
   *	   clarity's sake. The second member would end up returing true
   *	   nonetheless because, in that event,
   *	      latest_full == get_trim_to() == 0.
   */
  return (!latest_full ||
	  (latest_full <= get_trim_to()) ||
	  (get_last_committed() - latest_full > (unsigned)g_conf->paxos_stash_full_interval));
}

