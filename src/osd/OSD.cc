// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 * Copyright (C) 2017 OVH
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "acconfig.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>

#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <boost/scoped_ptr.hpp>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#include "osd/PG.h"

#include "include/types.h"
#include "include/compat.h"
#include "include/random.h"

#include "OSD.h"
#include "OSDMap.h"
#include "Watch.h"
#include "osdc/Objecter.h"

#include "common/errno.h"
#include "common/ceph_argparse.h"
#include "common/ceph_releases.h"
#include "common/ceph_time.h"
#include "common/version.h"
#include "common/pick_address.h"
#include "common/blkdev.h"
#include "common/numa.h"

#include "os/ObjectStore.h"
#ifdef HAVE_LIBFUSE
#include "os/FuseStore.h"
#endif

#include "PrimaryLogPG.h"

#include "msg/Messenger.h"
#include "msg/Message.h"

#include "mon/MonClient.h"

#include "messages/MLog.h"

#include "messages/MGenericMessage.h"
#include "messages/MOSDPing.h"
#include "messages/MOSDFailure.h"
#include "messages/MOSDMarkMeDown.h"
#include "messages/MOSDFull.h"
#include "messages/MOSDOp.h"
#include "messages/MOSDOpReply.h"
#include "messages/MOSDBackoff.h"
#include "messages/MOSDBeacon.h"
#include "messages/MOSDRepOp.h"
#include "messages/MOSDRepOpReply.h"
#include "messages/MOSDBoot.h"
#include "messages/MOSDPGTemp.h"
#include "messages/MOSDPGReadyToMerge.h"

#include "messages/MOSDMap.h"
#include "messages/MMonGetOSDMap.h"
#include "messages/MOSDPGNotify.h"
#include "messages/MOSDPGQuery.h"
#include "messages/MOSDPGLog.h"
#include "messages/MOSDPGRemove.h"
#include "messages/MOSDPGInfo.h"
#include "messages/MOSDPGCreate.h"
#include "messages/MOSDPGCreate2.h"
#include "messages/MOSDPGTrim.h"
#include "messages/MOSDPGScan.h"
#include "messages/MBackfillReserve.h"
#include "messages/MRecoveryReserve.h"
#include "messages/MOSDForceRecovery.h"
#include "messages/MOSDECSubOpWrite.h"
#include "messages/MOSDECSubOpWriteReply.h"
#include "messages/MOSDECSubOpRead.h"
#include "messages/MOSDECSubOpReadReply.h"
#include "messages/MOSDPGCreated.h"
#include "messages/MOSDPGUpdateLogMissing.h"
#include "messages/MOSDPGUpdateLogMissingReply.h"

#include "messages/MOSDPeeringOp.h"

#include "messages/MOSDAlive.h"

#include "messages/MOSDScrub.h"
#include "messages/MOSDScrub2.h"
#include "messages/MOSDRepScrub.h"

#include "messages/MMonCommand.h"
#include "messages/MCommand.h"
#include "messages/MCommandReply.h"

#include "messages/MPGStats.h"
#include "messages/MPGStatsAck.h"

#include "messages/MWatchNotify.h"
#include "messages/MOSDPGPush.h"
#include "messages/MOSDPGPushReply.h"
#include "messages/MOSDPGPull.h"

#include "messages/MMonGetPurgedSnaps.h"
#include "messages/MMonGetPurgedSnapsReply.h"

#include "common/perf_counters.h"
#include "common/Timer.h"
#include "common/LogClient.h"
#include "common/AsyncReserver.h"
#include "common/HeartbeatMap.h"
#include "common/admin_socket.h"
#include "common/ceph_context.h"

#include "global/signal_handler.h"
#include "global/pidfile.h"

#include "include/color.h"
#include "perfglue/cpu_profiler.h"
#include "perfglue/heap_profiler.h"

#include "osd/OpRequest.h"

#include "auth/AuthAuthorizeHandler.h"
#include "auth/RotatingKeyRing.h"

#include "objclass/objclass.h"

#include "common/cmdparse.h"
#include "include/str_list.h"
#include "include/util.h"

#include "include/ceph_assert.h"
#include "common/config.h"
#include "common/EventTrace.h"

#include "json_spirit/json_spirit_reader.h"
#include "json_spirit/json_spirit_writer.h"

#ifdef WITH_LTTNG
#define TRACEPOINT_DEFINE
#define TRACEPOINT_PROBE_DYNAMIC_LINKAGE
#include "tracing/osd.h"
#undef TRACEPOINT_PROBE_DYNAMIC_LINKAGE
#undef TRACEPOINT_DEFINE
#else
#define tracepoint(...)
#endif

#define dout_context cct
#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix _prefix(_dout, whoami, get_osdmap_epoch())


static ostream& _prefix(std::ostream* _dout, int whoami, epoch_t epoch) {
  return *_dout << "osd." << whoami << " " << epoch << " ";
}

//Initial features in new superblock.
//Features here are also automatically upgraded
CompatSet OSD::get_osd_initial_compat_set() {
  CompatSet::FeatureSet ceph_osd_feature_compat;
  CompatSet::FeatureSet ceph_osd_feature_ro_compat;
  CompatSet::FeatureSet ceph_osd_feature_incompat;
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_BASE);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_PGINFO);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_OLOC);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_LEC);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_CATEGORIES);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_HOBJECTPOOL);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_BIGINFO);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_LEVELDBINFO);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_LEVELDBLOG);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_SNAPMAPPER);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_HINTS);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_PGMETA);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_MISSING);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_FASTINFO);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_RECOVERY_DELETES);
  ceph_osd_feature_incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_SNAPMAPPER2);
  return CompatSet(ceph_osd_feature_compat, ceph_osd_feature_ro_compat,
		   ceph_osd_feature_incompat);
}

//Features are added here that this OSD supports.
CompatSet OSD::get_osd_compat_set() {
  CompatSet compat =  get_osd_initial_compat_set();
  //Any features here can be set in code, but not in initial superblock
  compat.incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_SHARDS);
  return compat;
}

OSDService::OSDService(OSD *osd) :
  osd(osd),
  cct(osd->cct),
  whoami(osd->whoami), store(osd->store),
  log_client(osd->log_client), clog(osd->clog),
  pg_recovery_stats(osd->pg_recovery_stats),
  cluster_messenger(osd->cluster_messenger),
  client_messenger(osd->client_messenger),
  logger(osd->logger),
  recoverystate_perf(osd->recoverystate_perf),
  monc(osd->monc),
  class_handler(osd->class_handler),
  osd_max_object_size(cct->_conf, "osd_max_object_size"),
  osd_skip_data_digest(cct->_conf, "osd_skip_data_digest"),
  publish_lock{ceph::make_mutex("OSDService::publish_lock")},
  pre_publish_lock{ceph::make_mutex("OSDService::pre_publish_lock")},
  max_oldest_map(0),
  sched_scrub_lock("OSDService::sched_scrub_lock"), scrubs_pending(0),
  scrubs_active(0),
  agent_lock("OSDService::agent_lock"),
  agent_valid_iterator(false),
  agent_ops(0),
  flush_mode_high_count(0),
  agent_active(true),
  agent_thread(this),
  agent_stop_flag(false),
  agent_timer_lock("OSDService::agent_timer_lock"),
  agent_timer(osd->client_messenger->cct, agent_timer_lock),
  last_recalibrate(ceph_clock_now()),
  promote_max_objects(0),
  promote_max_bytes(0),
  objecter(new Objecter(osd->client_messenger->cct, osd->objecter_messenger, osd->monc, NULL, 0, 0)),
  m_objecter_finishers(cct->_conf->osd_objecter_finishers),
  watch_lock("OSDService::watch_lock"),
  watch_timer(osd->client_messenger->cct, watch_lock),
  next_notif_id(0),
  recovery_request_lock("OSDService::recovery_request_lock"),
  recovery_request_timer(cct, recovery_request_lock, false),
  sleep_lock("OSDService::sleep_lock"),
  sleep_timer(cct, sleep_lock, false),
  reserver_finisher(cct),
  local_reserver(cct, &reserver_finisher, cct->_conf->osd_max_backfills,
		 cct->_conf->osd_min_recovery_priority),
  remote_reserver(cct, &reserver_finisher, cct->_conf->osd_max_backfills,
		  cct->_conf->osd_min_recovery_priority),
  pg_temp_lock("OSDService::pg_temp_lock"),
  snap_reserver(cct, &reserver_finisher,
		cct->_conf->osd_max_trimming_pgs),
  recovery_lock("OSDService::recovery_lock"),
  recovery_ops_active(0),
  recovery_ops_reserved(0),
  recovery_paused(false),
  map_cache_lock("OSDService::map_cache_lock"),
  map_cache(cct, cct->_conf->osd_map_cache_size),
  map_bl_cache(cct->_conf->osd_map_cache_size),
  map_bl_inc_cache(cct->_conf->osd_map_cache_size),
  stat_lock("OSDService::stat_lock"),
  full_status_lock("OSDService::full_status_lock"),
  cur_state(NONE),
  cur_ratio(0), physical_ratio(0),
  epoch_lock("OSDService::epoch_lock"),
  boot_epoch(0), up_epoch(0), bind_epoch(0)
#ifdef PG_DEBUG_REFS
  , pgid_lock("OSDService::pgid_lock")
#endif
{
  objecter->init();

  for (int i = 0; i < m_objecter_finishers; i++) {
    ostringstream str;
    str << "objecter-finisher-" << i;
    Finisher *fin = new Finisher(osd->client_messenger->cct, str.str(), "finisher");
    objecter_finishers.push_back(fin);
  }
}

OSDService::~OSDService()
{
  delete objecter;

  for (auto f : objecter_finishers) {
    delete f;
    f = NULL;
  }
}



#ifdef PG_DEBUG_REFS
void OSDService::add_pgid(spg_t pgid, PG *pg){
  std::lock_guard l(pgid_lock);
  if (!pgid_tracker.count(pgid)) {
    live_pgs[pgid] = pg;
  }
  pgid_tracker[pgid]++;
}
void OSDService::remove_pgid(spg_t pgid, PG *pg)
{
  std::lock_guard l(pgid_lock);
  ceph_assert(pgid_tracker.count(pgid));
  ceph_assert(pgid_tracker[pgid] > 0);
  pgid_tracker[pgid]--;
  if (pgid_tracker[pgid] == 0) {
    pgid_tracker.erase(pgid);
    live_pgs.erase(pgid);
  }
}
void OSDService::dump_live_pgids()
{
  std::lock_guard l(pgid_lock);
  derr << "live pgids:" << dendl;
  for (map<spg_t, int>::const_iterator i = pgid_tracker.cbegin();
       i != pgid_tracker.cend();
       ++i) {
    derr << "\t" << *i << dendl;
    live_pgs[i->first]->dump_live_ids();
  }
}
#endif



void OSDService::identify_splits_and_merges(
  OSDMapRef old_map,
  OSDMapRef new_map,
  spg_t pgid,
  set<pair<spg_t,epoch_t>> *split_children,
  set<pair<spg_t,epoch_t>> *merge_pgs)
{
  if (!old_map->have_pg_pool(pgid.pool())) {
    return;
  }
  int old_pgnum = old_map->get_pg_num(pgid.pool());
  auto p = osd->pg_num_history.pg_nums.find(pgid.pool());
  if (p == osd->pg_num_history.pg_nums.end()) {
    return;
  }
  dout(20) << __func__ << " " << pgid << " e" << old_map->get_epoch()
	   << " to e" << new_map->get_epoch()
	   << " pg_nums " << p->second << dendl;
  deque<spg_t> queue;
  queue.push_back(pgid);
  while (!queue.empty()) {
    auto cur = queue.front();
    queue.pop_front();
    unsigned pgnum = old_pgnum;
    for (auto q = p->second.lower_bound(old_map->get_epoch());
	 q != p->second.end() &&
	   q->first <= new_map->get_epoch();
	 ++q) {
      if (pgnum < q->second) {
	// split?
	if (cur.ps() < pgnum) {
	  set<spg_t> children;
	  if (cur.is_split(pgnum, q->second, &children)) {
	    dout(20) << __func__ << " " << cur << " e" << q->first
		     << " pg_num " << pgnum << " -> " << q->second
		     << " children " << children << dendl;
	    for (auto i : children) {
	      split_children->insert(make_pair(i, q->first));
	      queue.push_back(i);
	    }
	  }
	} else if (cur.ps() < q->second) {
	  dout(20) << __func__ << " " << cur << " e" << q->first
		   << " pg_num " << pgnum << " -> " << q->second
		   << " is a child" << dendl;
	  // normally we'd capture this from the parent, but it's
	  // possible the parent doesn't exist yet (it will be
	  // fabricated to allow an intervening merge).  note this PG
	  // as a split child here to be sure we catch it.
	  split_children->insert(make_pair(cur, q->first));
	} else {
	  dout(20) << __func__ << " " << cur << " e" << q->first
		   << " pg_num " << pgnum << " -> " << q->second
		   << " is post-split, skipping" << dendl;
	}
      } else if (merge_pgs) {
	// merge?
	if (cur.ps() >= q->second) {
	  if (cur.ps() < pgnum) {
	    spg_t parent;
	    if (cur.is_merge_source(pgnum, q->second, &parent)) {
	      set<spg_t> children;
	      parent.is_split(q->second, pgnum, &children);
	      dout(20) << __func__ << " " << cur << " e" << q->first
		       << " pg_num " << pgnum << " -> " << q->second
		       << " is merge source, target " << parent
		       << ", source(s) " << children << dendl;
	      merge_pgs->insert(make_pair(parent, q->first));
	      for (auto c : children) {
		merge_pgs->insert(make_pair(c, q->first));
	      }
	    }
	  } else {
	    dout(20) << __func__ << " " << cur << " e" << q->first
		     << " pg_num " << pgnum << " -> " << q->second
		     << " is beyond old pgnum, skipping" << dendl;
	  }
	} else {
	  set<spg_t> children;
	  if (cur.is_split(q->second, pgnum, &children)) {
	    dout(20) << __func__ << " " << cur << " e" << q->first
		     << " pg_num " << pgnum << " -> " << q->second
		     << " is merge target, source " << children << dendl;
	    for (auto c : children) {
	      merge_pgs->insert(make_pair(c, q->first));
	    }
	    merge_pgs->insert(make_pair(cur, q->first));
	  }
	}
      }
      pgnum = q->second;
    }
  }
}

void OSDService::need_heartbeat_peer_update()
{
  osd->need_heartbeat_peer_update();
}

void OSDService::start_shutdown()
{
  {
    std::lock_guard l(agent_timer_lock);
    agent_timer.shutdown();
  }

  {
    std::lock_guard l(sleep_lock);
    sleep_timer.shutdown();
  }

  {
    std::lock_guard l(recovery_request_lock);
    recovery_request_timer.shutdown();
  }
}

void OSDService::shutdown_reserver()
{
  reserver_finisher.wait_for_empty();
  reserver_finisher.stop();
}

void OSDService::shutdown()
{
  {
    std::lock_guard l(watch_lock);
    watch_timer.shutdown();
  }

  objecter->shutdown();
  for (auto f : objecter_finishers) {
    f->wait_for_empty();
    f->stop();
  }

  publish_map(OSDMapRef());
  next_osdmap = OSDMapRef();
}

void OSDService::init()
{
  reserver_finisher.start();
  for (auto f : objecter_finishers) {
    f->start();
  }
  objecter->set_client_incarnation(0);

  // deprioritize objecter in daemonperf output
  objecter->get_logger()->set_prio_adjust(-3);

  watch_timer.init();
  agent_timer.init();

  agent_thread.create("osd_srv_agent");

  if (cct->_conf->osd_recovery_delay_start)
    defer_recovery(cct->_conf->osd_recovery_delay_start);
}

void OSDService::final_init()
{
  objecter->start(osdmap.get());
}

void OSDService::activate_map()
{
  // wake/unwake the tiering agent
  agent_lock.Lock();
  agent_active =
    !osdmap->test_flag(CEPH_OSDMAP_NOTIERAGENT) &&
    osd->is_active();
  agent_cond.Signal();
  agent_lock.Unlock();
}

void OSDService::request_osdmap_update(epoch_t e)
{
  osd->osdmap_subscribe(e, false);
}

class AgentTimeoutCB : public Context {
  PGRef pg;
public:
  explicit AgentTimeoutCB(PGRef _pg) : pg(_pg) {}
  void finish(int) override {
    pg->agent_choose_mode_restart();
  }
};

void OSDService::agent_entry()
{
  dout(10) << __func__ << " start" << dendl;
  agent_lock.Lock();

  while (!agent_stop_flag) {
    if (agent_queue.empty()) {
      dout(20) << __func__ << " empty queue" << dendl;
      agent_cond.Wait(agent_lock);
      continue;
    }
    uint64_t level = agent_queue.rbegin()->first;
    set<PGRef>& top = agent_queue.rbegin()->second;
    dout(10) << __func__
	     << " tiers " << agent_queue.size()
	     << ", top is " << level
	     << " with pgs " << top.size()
	     << ", ops " << agent_ops << "/"
	     << cct->_conf->osd_agent_max_ops
	     << (agent_active ? " active" : " NOT ACTIVE")
	     << dendl;
    dout(20) << __func__ << " oids " << agent_oids << dendl;
    int max = cct->_conf->osd_agent_max_ops - agent_ops;
    int agent_flush_quota = max;
    if (!flush_mode_high_count)
      agent_flush_quota = cct->_conf->osd_agent_max_low_ops - agent_ops;
    if (agent_flush_quota <= 0 || top.empty() || !agent_active) {
      agent_cond.Wait(agent_lock);
      continue;
    }

    if (!agent_valid_iterator || agent_queue_pos == top.end()) {
      agent_queue_pos = top.begin();
      agent_valid_iterator = true;
    }
    PGRef pg = *agent_queue_pos;
    dout(10) << "high_count " << flush_mode_high_count
	     << " agent_ops " << agent_ops
	     << " flush_quota " << agent_flush_quota << dendl;
    agent_lock.Unlock();
    if (!pg->agent_work(max, agent_flush_quota)) {
      dout(10) << __func__ << " " << pg->pg_id
	<< " no agent_work, delay for " << cct->_conf->osd_agent_delay_time
	<< " seconds" << dendl;

      osd->logger->inc(l_osd_tier_delay);
      // Queue a timer to call agent_choose_mode for this pg in 5 seconds
      agent_timer_lock.Lock();
      Context *cb = new AgentTimeoutCB(pg);
      agent_timer.add_event_after(cct->_conf->osd_agent_delay_time, cb);
      agent_timer_lock.Unlock();
    }
    agent_lock.Lock();
  }
  agent_lock.Unlock();
  dout(10) << __func__ << " finish" << dendl;
}

void OSDService::agent_stop()
{
  {
    std::lock_guard l(agent_lock);

    // By this time all ops should be cancelled
    ceph_assert(agent_ops == 0);
    // By this time all PGs are shutdown and dequeued
    if (!agent_queue.empty()) {
      set<PGRef>& top = agent_queue.rbegin()->second;
      derr << "agent queue not empty, for example " << (*top.begin())->get_pgid() << dendl;
      ceph_abort_msg("agent queue not empty");
    }

    agent_stop_flag = true;
    agent_cond.Signal();
  }
  agent_thread.join();
}

// -------------------------------------

void OSDService::promote_throttle_recalibrate()
{
  utime_t now = ceph_clock_now();
  double dur = now - last_recalibrate;
  last_recalibrate = now;
  unsigned prob = promote_probability_millis;

  uint64_t target_obj_sec = cct->_conf->osd_tier_promote_max_objects_sec;
  uint64_t target_bytes_sec = cct->_conf->osd_tier_promote_max_bytes_sec;

  unsigned min_prob = 1;

  uint64_t attempts, obj, bytes;
  promote_counter.sample_and_attenuate(&attempts, &obj, &bytes);
  dout(10) << __func__ << " " << attempts << " attempts, promoted "
	   << obj << " objects and " << byte_u_t(bytes) << "; target "
	   << target_obj_sec << " obj/sec or "
	   << byte_u_t(target_bytes_sec) << "/sec"
	   << dendl;

  // calculate what the probability *should* be, given the targets
  unsigned new_prob;
  if (attempts && dur > 0) {
    uint64_t avg_size = 1;
    if (obj)
      avg_size = std::max<uint64_t>(bytes / obj, 1);
    unsigned po = (double)target_obj_sec * dur * 1000.0 / (double)attempts;
    unsigned pb = (double)target_bytes_sec / (double)avg_size * dur * 1000.0
      / (double)attempts;
    dout(20) << __func__ << "  po " << po << " pb " << pb << " avg_size "
	     << avg_size << dendl;
    if (target_obj_sec && target_bytes_sec)
      new_prob = std::min(po, pb);
    else if (target_obj_sec)
      new_prob = po;
    else if (target_bytes_sec)
      new_prob = pb;
    else
      new_prob = 1000;
  } else {
    new_prob = 1000;
  }
  dout(20) << __func__ << "  new_prob " << new_prob << dendl;

  // correct for persistent skew between target rate and actual rate, adjust
  double ratio = 1.0;
  unsigned actual = 0;
  if (attempts && obj) {
    actual = obj * 1000 / attempts;
    ratio = (double)actual / (double)prob;
    new_prob = (double)new_prob / ratio;
  }
  new_prob = std::max(new_prob, min_prob);
  new_prob = std::min(new_prob, 1000u);

  // adjust
  prob = (prob + new_prob) / 2;
  prob = std::max(prob, min_prob);
  prob = std::min(prob, 1000u);
  dout(10) << __func__ << "  actual " << actual
	   << ", actual/prob ratio " << ratio
	   << ", adjusted new_prob " << new_prob
	   << ", prob " << promote_probability_millis << " -> " << prob
	   << dendl;
  promote_probability_millis = prob;

  // set hard limits for this interval to mitigate stampedes
  promote_max_objects = target_obj_sec * osd->OSD_TICK_INTERVAL * 2;
  promote_max_bytes = target_bytes_sec * osd->OSD_TICK_INTERVAL * 2;
}

// -------------------------------------

float OSDService::get_failsafe_full_ratio()
{
  float full_ratio = cct->_conf->osd_failsafe_full_ratio;
  if (full_ratio > 1.0) full_ratio /= 100.0;
  return full_ratio;
}

OSDService::s_names OSDService::recalc_full_state(float ratio, float pratio, string &inject)
{
  // The OSDMap ratios take precendence.  So if the failsafe is .95 and
  // the admin sets the cluster full to .96, the failsafe moves up to .96
  // too.  (Not that having failsafe == full is ideal, but it's better than
  // dropping writes before the clusters appears full.)
  OSDMapRef osdmap = get_osdmap();
  if (!osdmap || osdmap->get_epoch() == 0) {
    return NONE;
  }
  float nearfull_ratio = osdmap->get_nearfull_ratio();
  float backfillfull_ratio = std::max(osdmap->get_backfillfull_ratio(), nearfull_ratio);
  float full_ratio = std::max(osdmap->get_full_ratio(), backfillfull_ratio);
  float failsafe_ratio = std::max(get_failsafe_full_ratio(), full_ratio);

  if (osdmap->require_osd_release < ceph_release_t::luminous) {
    // use the failsafe for nearfull and full; the mon isn't using the
    // flags anyway because we're mid-upgrade.
    full_ratio = failsafe_ratio;
    backfillfull_ratio = failsafe_ratio;
    nearfull_ratio = failsafe_ratio;
  } else if (full_ratio <= 0 ||
	     backfillfull_ratio <= 0 ||
	     nearfull_ratio <= 0) {
    derr << __func__ << " full_ratio, backfillfull_ratio or nearfull_ratio is <= 0" << dendl;
    // use failsafe flag.  ick.  the monitor did something wrong or the user
    // did something stupid.
    full_ratio = failsafe_ratio;
    backfillfull_ratio = failsafe_ratio;
    nearfull_ratio = failsafe_ratio;
  }

  if (injectfull_state > NONE && injectfull) {
    inject = "(Injected)";
    return injectfull_state;
  } else if (pratio > failsafe_ratio) {
    return FAILSAFE;
  } else if (ratio > full_ratio) {
    return FULL;
  } else if (ratio > backfillfull_ratio) {
    return BACKFILLFULL;
  } else if (ratio > nearfull_ratio) {
    return NEARFULL;
  }
   return NONE;
}

void OSDService::check_full_status(float ratio, float pratio)
{
  std::lock_guard l(full_status_lock);

  cur_ratio = ratio;
  physical_ratio = pratio;

  string inject;
  s_names new_state;
  new_state = recalc_full_state(ratio, pratio, inject);

  dout(20) << __func__ << " cur ratio " << ratio
           << ", physical ratio " << pratio
	   << ", new state " << get_full_state_name(new_state)
	   << " " << inject
	   << dendl;

  // warn
  if (cur_state != new_state) {
    dout(10) << __func__ << " " << get_full_state_name(cur_state)
	     << " -> " << get_full_state_name(new_state) << dendl;
    if (new_state == FAILSAFE) {
      clog->error() << "full status failsafe engaged, dropping updates, now "
		    << (int)roundf(ratio * 100) << "% full";
    } else if (cur_state == FAILSAFE) {
      clog->error() << "full status failsafe disengaged, no longer dropping "
		     << "updates, now " << (int)roundf(ratio * 100) << "% full";
    }
    cur_state = new_state;
  }
}

bool OSDService::need_fullness_update()
{
  OSDMapRef osdmap = get_osdmap();
  s_names cur = NONE;
  if (osdmap->exists(whoami)) {
    if (osdmap->get_state(whoami) & CEPH_OSD_FULL) {
      cur = FULL;
    } else if (osdmap->get_state(whoami) & CEPH_OSD_BACKFILLFULL) {
      cur = BACKFILLFULL;
    } else if (osdmap->get_state(whoami) & CEPH_OSD_NEARFULL) {
      cur = NEARFULL;
    }
  }
  s_names want = NONE;
  if (is_full())
    want = FULL;
  else if (is_backfillfull())
    want = BACKFILLFULL;
  else if (is_nearfull())
    want = NEARFULL;
  return want != cur;
}

bool OSDService::_check_inject_full(DoutPrefixProvider *dpp, s_names type) const
{
  if (injectfull && injectfull_state >= type) {
    // injectfull is either a count of the number of times to return failsafe full
    // or if -1 then always return full
    if (injectfull > 0)
      --injectfull;
    ldpp_dout(dpp, 10) << __func__ << " Injected " << get_full_state_name(type) << " OSD ("
             << (injectfull < 0 ? "set" : std::to_string(injectfull)) << ")"
             << dendl;
    return true;
  }
  return false;
}

bool OSDService::_check_full(DoutPrefixProvider *dpp, s_names type) const
{
  std::lock_guard l(full_status_lock);

  if (_check_inject_full(dpp, type))
    return true;

  if (cur_state >= type)
    ldpp_dout(dpp, 10) << __func__ << " current usage is " << cur_ratio
                       << " physical " << physical_ratio << dendl;

  return cur_state >= type;
}

bool OSDService::_tentative_full(DoutPrefixProvider *dpp, s_names type, uint64_t adjust_used, osd_stat_t adjusted_stat)
{
  ldpp_dout(dpp, 20) << __func__ << " type " << get_full_state_name(type) << " adjust_used " << (adjust_used >> 10) << "KiB" << dendl;
  {
    std::lock_guard l(full_status_lock);
    if (_check_inject_full(dpp, type)) {
      return true;
    }
  }

  float pratio;
  float ratio = compute_adjusted_ratio(adjusted_stat, &pratio, adjust_used);

  string notused;
  s_names tentative_state = recalc_full_state(ratio, pratio, notused);

  if (tentative_state >= type)
    ldpp_dout(dpp, 10) << __func__ << " tentative usage is " << ratio << dendl;

  return tentative_state >= type;
}

bool OSDService::check_failsafe_full(DoutPrefixProvider *dpp) const
{
  return _check_full(dpp, FAILSAFE);
}

bool OSDService::check_full(DoutPrefixProvider *dpp) const
{
  return _check_full(dpp, FULL);
}

bool OSDService::tentative_backfill_full(DoutPrefixProvider *dpp, uint64_t adjust_used, osd_stat_t stats)
{
  return _tentative_full(dpp, BACKFILLFULL, adjust_used, stats);
}

bool OSDService::check_backfill_full(DoutPrefixProvider *dpp) const
{
  return _check_full(dpp, BACKFILLFULL);
}

bool OSDService::check_nearfull(DoutPrefixProvider *dpp) const
{
  return _check_full(dpp, NEARFULL);
}

bool OSDService::is_failsafe_full() const
{
  std::lock_guard l(full_status_lock);
  return cur_state == FAILSAFE;
}

bool OSDService::is_full() const
{
  std::lock_guard l(full_status_lock);
  return cur_state >= FULL;
}

bool OSDService::is_backfillfull() const
{
  std::lock_guard l(full_status_lock);
  return cur_state >= BACKFILLFULL;
}

bool OSDService::is_nearfull() const
{
  std::lock_guard l(full_status_lock);
  return cur_state >= NEARFULL;
}

void OSDService::set_injectfull(s_names type, int64_t count)
{
  std::lock_guard l(full_status_lock);
  injectfull_state = type;
  injectfull = count;
}

void OSDService::set_statfs(const struct store_statfs_t &stbuf,
			    osd_alert_list_t& alerts)
{
  uint64_t bytes = stbuf.total;
  uint64_t avail = stbuf.available;
  uint64_t used = stbuf.get_used_raw();

  // For testing fake statfs values so it doesn't matter if all
  // OSDs are using the same partition.
  if (cct->_conf->fake_statfs_for_testing) {
    uint64_t total_num_bytes = 0;
    vector<PGRef> pgs;
    osd->_get_pgs(&pgs);
    for (auto p : pgs) {
      total_num_bytes += p->get_stats_num_bytes();
    }
    bytes = cct->_conf->fake_statfs_for_testing;
    if (total_num_bytes < bytes)
      avail = bytes - total_num_bytes;
    else
      avail = 0;
    dout(0) << __func__ << " fake total " << cct->_conf->fake_statfs_for_testing
            << " adjust available " << avail
            << dendl;
    used = bytes - avail;
  }

  osd->logger->set(l_osd_stat_bytes, bytes);
  osd->logger->set(l_osd_stat_bytes_used, used);
  osd->logger->set(l_osd_stat_bytes_avail, avail);

  std::lock_guard l(stat_lock);
  osd_stat.statfs = stbuf;
  osd_stat.os_alerts.clear();
  osd_stat.os_alerts[whoami].swap(alerts);
  if (cct->_conf->fake_statfs_for_testing) {
    osd_stat.statfs.total = bytes;
    osd_stat.statfs.available = avail;
    // For testing don't want used to go negative, so clear reserved
    osd_stat.statfs.internally_reserved = 0;
  }
}

osd_stat_t OSDService::set_osd_stat(vector<int>& hb_peers,
				    int num_pgs)
{
  std::lock_guard l(stat_lock);
  osd_stat.hb_peers.swap(hb_peers);
  osd->op_tracker.get_age_ms_histogram(&osd_stat.op_queue_age_hist);
  osd_stat.num_pgs = num_pgs;
  return osd_stat;
}

void OSDService::inc_osd_stat_repaired()
{
  std::lock_guard l(stat_lock);
  osd_stat.num_shards_repaired++;
  return;
}

float OSDService::compute_adjusted_ratio(osd_stat_t new_stat, float *pratio,
				         uint64_t adjust_used)
{
  *pratio =
   ((float)new_stat.statfs.get_used()) / ((float)new_stat.statfs.total);

  if (adjust_used) {
    dout(20) << __func__ << " Before kb_used() " << new_stat.statfs.kb_used()  << dendl;
    if (new_stat.statfs.available > adjust_used)
      new_stat.statfs.available -= adjust_used;
    else
      new_stat.statfs.available = 0;
    dout(20) << __func__ << " After kb_used() " << new_stat.statfs.kb_used() << dendl;
  }

  // Check all pgs and adjust kb_used to include all pending backfill data
  int backfill_adjusted = 0;
  vector<PGRef> pgs;
  osd->_get_pgs(&pgs);
  for (auto p : pgs) {
    backfill_adjusted += p->pg_stat_adjust(&new_stat);
  }
  if (backfill_adjusted) {
    dout(20) << __func__ << " backfill adjusted " << new_stat << dendl;
  }
  return ((float)new_stat.statfs.get_used()) / ((float)new_stat.statfs.total);
}

void OSDService::send_message_osd_cluster(int peer, Message *m, epoch_t from_epoch)
{
  OSDMapRef next_map = get_nextmap_reserved();
  // service map is always newer/newest
  ceph_assert(from_epoch <= next_map->get_epoch());

  if (next_map->is_down(peer) ||
      next_map->get_info(peer).up_from > from_epoch) {
    m->put();
    release_map(next_map);
    return;
  }
  ConnectionRef peer_con = osd->cluster_messenger->connect_to_osd(
    next_map->get_cluster_addrs(peer));
  maybe_share_map(peer_con.get(), next_map);
  peer_con->send_message(m);
  release_map(next_map);
}

ConnectionRef OSDService::get_con_osd_cluster(int peer, epoch_t from_epoch)
{
  OSDMapRef next_map = get_nextmap_reserved();
  // service map is always newer/newest
  ceph_assert(from_epoch <= next_map->get_epoch());

  if (next_map->is_down(peer) ||
      next_map->get_info(peer).up_from > from_epoch) {
    release_map(next_map);
    return NULL;
  }
  ConnectionRef con = osd->cluster_messenger->connect_to_osd(
    next_map->get_cluster_addrs(peer));
  release_map(next_map);
  return con;
}

pair<ConnectionRef,ConnectionRef> OSDService::get_con_osd_hb(int peer, epoch_t from_epoch)
{
  OSDMapRef next_map = get_nextmap_reserved();
  // service map is always newer/newest
  ceph_assert(from_epoch <= next_map->get_epoch());

  pair<ConnectionRef,ConnectionRef> ret;
  if (next_map->is_down(peer) ||
      next_map->get_info(peer).up_from > from_epoch) {
    release_map(next_map);
    return ret;
  }
  ret.first = osd->hb_back_client_messenger->connect_to_osd(
    next_map->get_hb_back_addrs(peer));
  ret.second = osd->hb_front_client_messenger->connect_to_osd(
    next_map->get_hb_front_addrs(peer));
  release_map(next_map);
  return ret;
}

entity_name_t OSDService::get_cluster_msgr_name() const
{
  return cluster_messenger->get_myname();
}

void OSDService::queue_want_pg_temp(pg_t pgid,
				    const vector<int>& want,
				    bool forced)
{
  std::lock_guard l(pg_temp_lock);
  auto p = pg_temp_pending.find(pgid);
  if (p == pg_temp_pending.end() ||
      p->second.acting != want ||
      forced) {
    pg_temp_wanted[pgid] = {want, forced};
  }
}

void OSDService::remove_want_pg_temp(pg_t pgid)
{
  std::lock_guard l(pg_temp_lock);
  pg_temp_wanted.erase(pgid);
  pg_temp_pending.erase(pgid);
}

void OSDService::_sent_pg_temp()
{
#ifdef HAVE_STDLIB_MAP_SPLICING
  pg_temp_pending.merge(pg_temp_wanted);
#else
  pg_temp_pending.insert(make_move_iterator(begin(pg_temp_wanted)),
			 make_move_iterator(end(pg_temp_wanted)));
#endif
  pg_temp_wanted.clear();
}

void OSDService::requeue_pg_temp()
{
  std::lock_guard l(pg_temp_lock);
  // wanted overrides pending.  note that remove_want_pg_temp
  // clears the item out of both.
  unsigned old_wanted = pg_temp_wanted.size();
  unsigned old_pending = pg_temp_pending.size();
  _sent_pg_temp();
  pg_temp_wanted.swap(pg_temp_pending);
  dout(10) << __func__ << " " << old_wanted << " + " << old_pending << " -> "
	   << pg_temp_wanted.size() << dendl;
}

std::ostream& operator<<(std::ostream& out,
			 const OSDService::pg_temp_t& pg_temp)
{
  out << pg_temp.acting;
  if (pg_temp.forced) {
    out << " (forced)";
  }
  return out;
}

void OSDService::send_pg_temp()
{
  std::lock_guard l(pg_temp_lock);
  if (pg_temp_wanted.empty())
    return;
  dout(10) << "send_pg_temp " << pg_temp_wanted << dendl;
  MOSDPGTemp *ms[2] = {nullptr, nullptr};
  for (auto& [pgid, pg_temp] : pg_temp_wanted) {
    auto& m = ms[pg_temp.forced];
    if (!m) {
      m = new MOSDPGTemp(osdmap->get_epoch());
      m->forced = pg_temp.forced;
    }
    m->pg_temp.emplace(pgid, pg_temp.acting);
  }
  for (auto m : ms) {
    if (m) {
      monc->send_mon_message(m);
    }
  }
  _sent_pg_temp();
}

void OSDService::send_pg_created(pg_t pgid)
{
  std::lock_guard l(pg_created_lock);
  dout(20) << __func__ << dendl;
  auto o = get_osdmap();
  if (o->require_osd_release >= ceph_release_t::luminous) {
    pg_created.insert(pgid);
    monc->send_mon_message(new MOSDPGCreated(pgid));
  }
}

void OSDService::send_pg_created()
{
  std::lock_guard l(pg_created_lock);
  dout(20) << __func__ << dendl;
  auto o = get_osdmap();
  if (o->require_osd_release >= ceph_release_t::luminous) {
    for (auto pgid : pg_created) {
      monc->send_mon_message(new MOSDPGCreated(pgid));
    }
  }
}

void OSDService::prune_pg_created()
{
  std::lock_guard l(pg_created_lock);
  dout(20) << __func__ << dendl;
  auto o = get_osdmap();
  auto i = pg_created.begin();
  while (i != pg_created.end()) {
    auto p = o->get_pg_pool(i->pool());
    if (!p || !p->has_flag(pg_pool_t::FLAG_CREATING)) {
      dout(20) << __func__ << " pruning " << *i << dendl;
      i = pg_created.erase(i);
    } else {
      dout(20) << __func__ << " keeping " << *i << dendl;
      ++i;
    }
  }
}


// --------------------------------------
// dispatch

bool OSDService::can_inc_scrubs_pending()
{
  bool can_inc = false;
  std::lock_guard l(sched_scrub_lock);

  if (scrubs_pending + scrubs_active < cct->_conf->osd_max_scrubs) {
    dout(20) << __func__ << " " << scrubs_pending << " -> " << (scrubs_pending+1)
	     << " (max " << cct->_conf->osd_max_scrubs << ", active " << scrubs_active
	     << ")" << dendl;
    can_inc = true;
  } else {
    dout(20) << __func__ << " " << scrubs_pending << " + " << scrubs_active
	     << " active >= max " << cct->_conf->osd_max_scrubs << dendl;
  }

  return can_inc;
}

bool OSDService::inc_scrubs_pending()
{
  bool result = false;

  sched_scrub_lock.Lock();
  if (scrubs_pending + scrubs_active < cct->_conf->osd_max_scrubs) {
    dout(20) << "inc_scrubs_pending " << scrubs_pending << " -> " << (scrubs_pending+1)
	     << " (max " << cct->_conf->osd_max_scrubs << ", active " << scrubs_active << ")" << dendl;
    result = true;
    ++scrubs_pending;
  } else {
    dout(20) << "inc_scrubs_pending " << scrubs_pending << " + " << scrubs_active << " active >= max " << cct->_conf->osd_max_scrubs << dendl;
  }
  sched_scrub_lock.Unlock();

  return result;
}

void OSDService::dec_scrubs_pending()
{
  sched_scrub_lock.Lock();
  dout(20) << "dec_scrubs_pending " << scrubs_pending << " -> " << (scrubs_pending-1)
	   << " (max " << cct->_conf->osd_max_scrubs << ", active " << scrubs_active << ")" << dendl;
  --scrubs_pending;
  ceph_assert(scrubs_pending >= 0);
  sched_scrub_lock.Unlock();
}

void OSDService::inc_scrubs_active(bool reserved)
{
  sched_scrub_lock.Lock();
  ++(scrubs_active);
  if (reserved) {
    --(scrubs_pending);
    dout(20) << "inc_scrubs_active " << (scrubs_active-1) << " -> " << scrubs_active
	     << " (max " << cct->_conf->osd_max_scrubs
	     << ", pending " << (scrubs_pending+1) << " -> " << scrubs_pending << ")" << dendl;
    ceph_assert(scrubs_pending >= 0);
  } else {
    dout(20) << "inc_scrubs_active " << (scrubs_active-1) << " -> " << scrubs_active
	     << " (max " << cct->_conf->osd_max_scrubs
	     << ", pending " << scrubs_pending << ")" << dendl;
  }
  sched_scrub_lock.Unlock();
}

void OSDService::dec_scrubs_active()
{
  sched_scrub_lock.Lock();
  dout(20) << "dec_scrubs_active " << scrubs_active << " -> " << (scrubs_active-1)
	   << " (max " << cct->_conf->osd_max_scrubs << ", pending " << scrubs_pending << ")" << dendl;
  --scrubs_active;
  ceph_assert(scrubs_active >= 0);
  sched_scrub_lock.Unlock();
}

void OSDService::retrieve_epochs(epoch_t *_boot_epoch, epoch_t *_up_epoch,
                                 epoch_t *_bind_epoch) const
{
  std::lock_guard l(epoch_lock);
  if (_boot_epoch)
    *_boot_epoch = boot_epoch;
  if (_up_epoch)
    *_up_epoch = up_epoch;
  if (_bind_epoch)
    *_bind_epoch = bind_epoch;
}

void OSDService::set_epochs(const epoch_t *_boot_epoch, const epoch_t *_up_epoch,
                            const epoch_t *_bind_epoch)
{
  std::lock_guard l(epoch_lock);
  if (_boot_epoch) {
    ceph_assert(*_boot_epoch == 0 || *_boot_epoch >= boot_epoch);
    boot_epoch = *_boot_epoch;
  }
  if (_up_epoch) {
    ceph_assert(*_up_epoch == 0 || *_up_epoch >= up_epoch);
    up_epoch = *_up_epoch;
  }
  if (_bind_epoch) {
    ceph_assert(*_bind_epoch == 0 || *_bind_epoch >= bind_epoch);
    bind_epoch = *_bind_epoch;
  }
}

bool OSDService::prepare_to_stop()
{
  std::unique_lock l(is_stopping_lock);
  if (get_state() != NOT_STOPPING)
    return false;

  OSDMapRef osdmap = get_osdmap();
  if (osdmap && osdmap->is_up(whoami)) {
    dout(0) << __func__ << " telling mon we are shutting down" << dendl;
    set_state(PREPARING_TO_STOP);
    monc->send_mon_message(
      new MOSDMarkMeDown(
	monc->get_fsid(),
	whoami,
	osdmap->get_addrs(whoami),
	osdmap->get_epoch(),
	true  // request ack
	));
    const auto timeout = ceph::make_timespan(cct->_conf->osd_mon_shutdown_timeout);
    is_stopping_cond.wait_for(l, timeout,
      [this] { return get_state() == STOPPING; });
  }
  dout(0) << __func__ << " starting shutdown" << dendl;
  set_state(STOPPING);
  return true;
}

void OSDService::got_stop_ack()
{
  std::scoped_lock l(is_stopping_lock);
  if (get_state() == PREPARING_TO_STOP) {
    dout(0) << __func__ << " starting shutdown" << dendl;
    set_state(STOPPING);
    is_stopping_cond.notify_all();
  } else {
    dout(10) << __func__ << " ignoring msg" << dendl;
  }
}

MOSDMap *OSDService::build_incremental_map_msg(epoch_t since, epoch_t to,
                                               OSDSuperblock& sblock)
{
  MOSDMap *m = new MOSDMap(monc->get_fsid(),
			   osdmap->get_encoding_features());
  m->oldest_map = max_oldest_map;
  m->newest_map = sblock.newest_map;

  int max = cct->_conf->osd_map_message_max;
  ssize_t max_bytes = cct->_conf->osd_map_message_max_bytes;

  if (since < m->oldest_map) {
    // we don't have the next map the target wants, so start with a
    // full map.
    bufferlist bl;
    dout(10) << __func__ << " oldest map " << max_oldest_map << " > since "
	     << since << ", starting with full map" << dendl;
    since = m->oldest_map;
    if (!get_map_bl(since, bl)) {
      derr << __func__ << " missing full map " << since << dendl;
      goto panic;
    }
    max--;
    max_bytes -= bl.length();
    m->maps[since].claim(bl);
  }
  for (epoch_t e = since + 1; e <= to; ++e) {
    bufferlist bl;
    if (get_inc_map_bl(e, bl)) {
      m->incremental_maps[e].claim(bl);
    } else {
      derr << __func__ << " missing incremental map " << e << dendl;
      if (!get_map_bl(e, bl)) {
	derr << __func__ << " also missing full map " << e << dendl;
	goto panic;
      }
      m->maps[e].claim(bl);
    }
    max--;
    max_bytes -= bl.length();
    if (max <= 0 || max_bytes <= 0) {
      break;
    }
  }
  return m;

 panic:
  if (!m->maps.empty() ||
      !m->incremental_maps.empty()) {
    // send what we have so far
    return m;
  }
  // send something
  bufferlist bl;
  if (get_inc_map_bl(m->newest_map, bl)) {
    m->incremental_maps[m->newest_map].claim(bl);
  } else {
    derr << __func__ << " unable to load latest map " << m->newest_map << dendl;
    if (!get_map_bl(m->newest_map, bl)) {
      derr << __func__ << " unable to load latest full map " << m->newest_map
	   << dendl;
      ceph_abort();
    }
    m->maps[m->newest_map].claim(bl);
  }
  return m;
}

void OSDService::send_map(MOSDMap *m, Connection *con)
{
  con->send_message(m);
}

void OSDService::send_incremental_map(epoch_t since, Connection *con,
                                      const OSDMapRef& osdmap)
{
  epoch_t to = osdmap->get_epoch();
  dout(10) << "send_incremental_map " << since << " -> " << to
           << " to " << con << " " << con->get_peer_addr() << dendl;

  MOSDMap *m = NULL;
  while (!m) {
    OSDSuperblock sblock(get_superblock());
    if (since < sblock.oldest_map) {
      // just send latest full map
      MOSDMap *m = new MOSDMap(monc->get_fsid(),
			       osdmap->get_encoding_features());
      m->oldest_map = max_oldest_map;
      m->newest_map = sblock.newest_map;
      get_map_bl(to, m->maps[to]);
      send_map(m, con);
      return;
    }

    if (to > since && (int64_t)(to - since) > cct->_conf->osd_map_share_max_epochs) {
      dout(10) << "  " << (to - since) << " > max " << cct->_conf->osd_map_share_max_epochs
	       << ", only sending most recent" << dendl;
      since = to - cct->_conf->osd_map_share_max_epochs;
    }

    m = build_incremental_map_msg(since, to, sblock);
  }
  send_map(m, con);
}

bool OSDService::_get_map_bl(epoch_t e, bufferlist& bl)
{
  bool found = map_bl_cache.lookup(e, &bl);
  if (found) {
    if (logger)
      logger->inc(l_osd_map_bl_cache_hit);
    return true;
  }
  if (logger)
    logger->inc(l_osd_map_bl_cache_miss);
  found = store->read(meta_ch,
		      OSD::get_osdmap_pobject_name(e), 0, 0, bl,
		      CEPH_OSD_OP_FLAG_FADVISE_WILLNEED) >= 0;
  if (found) {
    _add_map_bl(e, bl);
  }
  return found;
}

bool OSDService::get_inc_map_bl(epoch_t e, bufferlist& bl)
{
  std::lock_guard l(map_cache_lock);
  bool found = map_bl_inc_cache.lookup(e, &bl);
  if (found) {
    if (logger)
      logger->inc(l_osd_map_bl_cache_hit);
    return true;
  }
  if (logger)
    logger->inc(l_osd_map_bl_cache_miss);
  found = store->read(meta_ch,
		      OSD::get_inc_osdmap_pobject_name(e), 0, 0, bl,
		      CEPH_OSD_OP_FLAG_FADVISE_WILLNEED) >= 0;
  if (found) {
    _add_map_inc_bl(e, bl);
  }
  return found;
}

void OSDService::_add_map_bl(epoch_t e, bufferlist& bl)
{
  dout(10) << "add_map_bl " << e << " " << bl.length() << " bytes" << dendl;
  // cache a contiguous buffer
  if (bl.get_num_buffers() > 1) {
    bl.rebuild();
  }
  bl.try_assign_to_mempool(mempool::mempool_osd_mapbl);
  map_bl_cache.add(e, bl);
}

void OSDService::_add_map_inc_bl(epoch_t e, bufferlist& bl)
{
  dout(10) << "add_map_inc_bl " << e << " " << bl.length() << " bytes" << dendl;
  // cache a contiguous buffer
  if (bl.get_num_buffers() > 1) {
    bl.rebuild();
  }
  bl.try_assign_to_mempool(mempool::mempool_osd_mapbl);
  map_bl_inc_cache.add(e, bl);
}

int OSDService::get_deleted_pool_pg_num(int64_t pool)
{
  std::lock_guard l(map_cache_lock);
  auto p = deleted_pool_pg_nums.find(pool);
  if (p != deleted_pool_pg_nums.end()) {
    return p->second;
  }
  dout(20) << __func__ << " " << pool << " loading" << dendl;
  ghobject_t oid = OSD::make_final_pool_info_oid(pool);
  bufferlist bl;
  int r = store->read(meta_ch, oid, 0, 0, bl);
  ceph_assert(r >= 0);
  auto blp = bl.cbegin();
  pg_pool_t pi;
  ::decode(pi, blp);
  deleted_pool_pg_nums[pool] = pi.get_pg_num();
  dout(20) << __func__ << " " << pool << " got " << pi.get_pg_num() << dendl;
  return pi.get_pg_num();
}

OSDMapRef OSDService::_add_map(OSDMap *o)
{
  epoch_t e = o->get_epoch();

  if (cct->_conf->osd_map_dedup) {
    // Dedup against an existing map at a nearby epoch
    OSDMapRef for_dedup = map_cache.lower_bound(e);
    if (for_dedup) {
      OSDMap::dedup(for_dedup.get(), o);
    }
  }
  bool existed;
  OSDMapRef l = map_cache.add(e, o, &existed);
  if (existed) {
    delete o;
  }
  return l;
}

OSDMapRef OSDService::try_get_map(epoch_t epoch)
{
  std::lock_guard l(map_cache_lock);
  OSDMapRef retval = map_cache.lookup(epoch);
  if (retval) {
    dout(30) << "get_map " << epoch << " -cached" << dendl;
    if (logger) {
      logger->inc(l_osd_map_cache_hit);
    }
    return retval;
  }
  if (logger) {
    logger->inc(l_osd_map_cache_miss);
    epoch_t lb = map_cache.cached_key_lower_bound();
    if (epoch < lb) {
      dout(30) << "get_map " << epoch << " - miss, below lower bound" << dendl;
      logger->inc(l_osd_map_cache_miss_low);
      logger->inc(l_osd_map_cache_miss_low_avg, lb - epoch);
    }
  }

  OSDMap *map = new OSDMap;
  if (epoch > 0) {
    dout(20) << "get_map " << epoch << " - loading and decoding " << map << dendl;
    bufferlist bl;
    if (!_get_map_bl(epoch, bl) || bl.length() == 0) {
      derr << "failed to load OSD map for epoch " << epoch << ", got " << bl.length() << " bytes" << dendl;
      delete map;
      return OSDMapRef();
    }
    map->decode(bl);
  } else {
    dout(20) << "get_map " << epoch << " - return initial " << map << dendl;
  }
  return _add_map(map);
}

// ops


void OSDService::reply_op_error(OpRequestRef op, int err)
{
  reply_op_error(op, err, eversion_t(), 0);
}

void OSDService::reply_op_error(OpRequestRef op, int err, eversion_t v,
                                version_t uv)
{
  const MOSDOp *m = static_cast<const MOSDOp*>(op->get_req());
  ceph_assert(m->get_type() == CEPH_MSG_OSD_OP);
  int flags;
  flags = m->get_flags() & (CEPH_OSD_FLAG_ACK|CEPH_OSD_FLAG_ONDISK);

  MOSDOpReply *reply = new MOSDOpReply(m, err, osdmap->get_epoch(), flags, err >= 0);
  reply->set_reply_versions(v, uv);
  m->get_connection()->send_message(reply);
}

void OSDService::handle_misdirected_op(PG *pg, OpRequestRef op)
{
  if (!cct->_conf->osd_debug_misdirected_ops) {
    return;
  }

  const MOSDOp *m = static_cast<const MOSDOp*>(op->get_req());
  ceph_assert(m->get_type() == CEPH_MSG_OSD_OP);

  ceph_assert(m->get_map_epoch() >= pg->get_history().same_primary_since);

  if (pg->is_ec_pg()) {
    /**
       * OSD recomputes op target based on current OSDMap. With an EC pg, we
       * can get this result:
       * 1) client at map 512 sends an op to osd 3, pg_t 3.9 based on mapping
       *    [CRUSH_ITEM_NONE, 2, 3]/3
       * 2) OSD 3 at map 513 remaps op to osd 3, spg_t 3.9s0 based on mapping
       *    [3, 2, 3]/3
       * 3) PG 3.9s0 dequeues the op at epoch 512 and notices that it isn't primary
       *    -- misdirected op
       * 4) client resends and this time PG 3.9s0 having caught up to 513 gets
       *    it and fulfils it
       *
       * We can't compute the op target based on the sending map epoch due to
       * splitting.  The simplest thing is to detect such cases here and drop
       * them without an error (the client will resend anyway).
       */
    ceph_assert(m->get_map_epoch() <= superblock.newest_map);
    OSDMapRef opmap = try_get_map(m->get_map_epoch());
    if (!opmap) {
      dout(7) << __func__ << ": " << *pg << " no longer have map for "
	      << m->get_map_epoch() << ", dropping" << dendl;
      return;
    }
    pg_t _pgid = m->get_raw_pg();
    spg_t pgid;
    if ((m->get_flags() & CEPH_OSD_FLAG_PGOP) == 0)
      _pgid = opmap->raw_pg_to_pg(_pgid);
    if (opmap->get_primary_shard(_pgid, &pgid) &&
	pgid.shard != pg->pg_id.shard) {
      dout(7) << __func__ << ": " << *pg << " primary changed since "
	      << m->get_map_epoch() << ", dropping" << dendl;
      return;
    }
  }

  dout(7) << *pg << " misdirected op in " << m->get_map_epoch() << dendl;
  clog->warn() << m->get_source_inst() << " misdirected " << m->get_reqid()
	       << " pg " << m->get_raw_pg()
	       << " to osd." << whoami
	       << " not " << pg->get_acting()
	       << " in e" << m->get_map_epoch() << "/" << osdmap->get_epoch();
}

void OSDService::enqueue_back(OpQueueItem&& qi)
{
  osd->op_shardedwq.queue(std::move(qi));
}

void OSDService::enqueue_front(OpQueueItem&& qi)
{
  osd->op_shardedwq.queue_front(std::move(qi));
}

void OSDService::queue_recovery_context(
  PG *pg,
  GenContext<ThreadPool::TPHandle&> *c)
{
  epoch_t e = get_osdmap_epoch();
  enqueue_back(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(
	new PGRecoveryContext(pg->get_pgid(), c, e)),
      cct->_conf->osd_recovery_cost,
      cct->_conf->osd_recovery_priority,
      ceph_clock_now(),
      0,
      e));
}

void OSDService::queue_for_snap_trim(PG *pg)
{
  dout(10) << "queueing " << *pg << " for snaptrim" << dendl;
  enqueue_back(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(
	new PGSnapTrim(pg->get_pgid(), pg->get_osdmap_epoch())),
      cct->_conf->osd_snap_trim_cost,
      cct->_conf->osd_snap_trim_priority,
      ceph_clock_now(),
      0,
      pg->get_osdmap_epoch()));
}

void OSDService::queue_for_scrub(PG *pg, bool with_high_priority)
{
  unsigned scrub_queue_priority = pg->scrubber.priority;
  if (with_high_priority && scrub_queue_priority < cct->_conf->osd_client_op_priority) {
    scrub_queue_priority = cct->_conf->osd_client_op_priority;
  }
  const auto epoch = pg->get_osdmap_epoch();
  enqueue_back(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(new PGScrub(pg->get_pgid(), epoch)),
      cct->_conf->osd_scrub_cost,
      scrub_queue_priority,
      ceph_clock_now(),
      0,
      epoch));
}

void OSDService::queue_for_pg_delete(spg_t pgid, epoch_t e)
{
  dout(10) << __func__ << " on " << pgid << " e " << e  << dendl;
  enqueue_back(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(
	new PGDelete(pgid, e)),
      cct->_conf->osd_pg_delete_cost,
      cct->_conf->osd_pg_delete_priority,
      ceph_clock_now(),
      0,
      e));
}

bool OSDService::try_finish_pg_delete(PG *pg, unsigned old_pg_num)
{
  return osd->try_finish_pg_delete(pg, old_pg_num);
}

// ---

void OSDService::set_ready_to_merge_source(PG *pg, eversion_t version)
{
  std::lock_guard l(merge_lock);
  dout(10) << __func__ << " " << pg->pg_id << dendl;
  ready_to_merge_source[pg->pg_id.pgid] = version;
  assert(not_ready_to_merge_source.count(pg->pg_id.pgid) == 0);
  _send_ready_to_merge();
}

void OSDService::set_ready_to_merge_target(PG *pg,
					   eversion_t version,
					   epoch_t last_epoch_started,
					   epoch_t last_epoch_clean)
{
  std::lock_guard l(merge_lock);
  dout(10) << __func__ << " " << pg->pg_id << dendl;
  ready_to_merge_target.insert(make_pair(pg->pg_id.pgid,
					 make_tuple(version,
						    last_epoch_started,
						    last_epoch_clean)));
  assert(not_ready_to_merge_target.count(pg->pg_id.pgid) == 0);
  _send_ready_to_merge();
}

void OSDService::set_not_ready_to_merge_source(pg_t source)
{
  std::lock_guard l(merge_lock);
  dout(10) << __func__ << " " << source << dendl;
  not_ready_to_merge_source.insert(source);
  assert(ready_to_merge_source.count(source) == 0);
  _send_ready_to_merge();
}

void OSDService::set_not_ready_to_merge_target(pg_t target, pg_t source)
{
  std::lock_guard l(merge_lock);
  dout(10) << __func__ << " " << target << " source " << source << dendl;
  not_ready_to_merge_target[target] = source;
  assert(ready_to_merge_target.count(target) == 0);
  _send_ready_to_merge();
}

void OSDService::send_ready_to_merge()
{
  std::lock_guard l(merge_lock);
  _send_ready_to_merge();
}

void OSDService::_send_ready_to_merge()
{
  dout(20) << __func__
	   << " ready_to_merge_source " << ready_to_merge_source
    	   << " not_ready_to_merge_source " << not_ready_to_merge_source
	   << " ready_to_merge_target " << ready_to_merge_target
    	   << " not_ready_to_merge_target " << not_ready_to_merge_target
	   << " sent_ready_to_merge_source " << sent_ready_to_merge_source
	   << dendl;
  for (auto src : not_ready_to_merge_source) {
    if (sent_ready_to_merge_source.count(src) == 0) {
      monc->send_mon_message(new MOSDPGReadyToMerge(
			       src,
			       {}, {}, 0, 0,
			       false,
			       osdmap->get_epoch()));
      sent_ready_to_merge_source.insert(src);
    }
  }
  for (auto p : not_ready_to_merge_target) {
    if (sent_ready_to_merge_source.count(p.second) == 0) {
      monc->send_mon_message(new MOSDPGReadyToMerge(
			       p.second,
			       {}, {}, 0, 0,
			       false,
			       osdmap->get_epoch()));
      sent_ready_to_merge_source.insert(p.second);
    }
  }
  for (auto src : ready_to_merge_source) {
    if (not_ready_to_merge_source.count(src.first) ||
	not_ready_to_merge_target.count(src.first.get_parent())) {
      continue;
    }
    auto p = ready_to_merge_target.find(src.first.get_parent());
    if (p != ready_to_merge_target.end() &&
	sent_ready_to_merge_source.count(src.first) == 0) {
      monc->send_mon_message(new MOSDPGReadyToMerge(
			       src.first,           // source pgid
			       src.second,          // src version
			       std::get<0>(p->second), // target version
			       std::get<1>(p->second), // PG's last_epoch_started
			       std::get<2>(p->second), // PG's last_epoch_clean
			       true,
			       osdmap->get_epoch()));
      sent_ready_to_merge_source.insert(src.first);
    }
  }
}

void OSDService::clear_ready_to_merge(PG *pg)
{
  std::lock_guard l(merge_lock);
  dout(10) << __func__ << " " << pg->pg_id << dendl;
  ready_to_merge_source.erase(pg->pg_id.pgid);
  ready_to_merge_target.erase(pg->pg_id.pgid);
  not_ready_to_merge_source.erase(pg->pg_id.pgid);
  not_ready_to_merge_target.erase(pg->pg_id.pgid);
  sent_ready_to_merge_source.erase(pg->pg_id.pgid);
}

void OSDService::clear_sent_ready_to_merge()
{
  std::lock_guard l(merge_lock);
  sent_ready_to_merge_source.clear();
}

void OSDService::prune_sent_ready_to_merge(OSDMapRef& osdmap)
{
  std::lock_guard l(merge_lock);
  auto i = sent_ready_to_merge_source.begin();
  while (i != sent_ready_to_merge_source.end()) {
    if (!osdmap->pg_exists(*i)) {
      dout(10) << __func__ << " " << *i << dendl;
      i = sent_ready_to_merge_source.erase(i);
    } else {
      ++i;
    }
  }
}

// ---

void OSDService::_queue_for_recovery(
  std::pair<epoch_t, PGRef> p,
  uint64_t reserved_pushes)
{
  ceph_assert(recovery_lock.is_locked_by_me());
  enqueue_back(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(
	new PGRecovery(
	  p.second->get_pgid(), p.first, reserved_pushes)),
      cct->_conf->osd_recovery_cost,
      cct->_conf->osd_recovery_priority,
      ceph_clock_now(),
      0,
      p.first));
}

// ====================================================================
// OSD

#undef dout_prefix
#define dout_prefix *_dout

// Commands shared between OSD's console and admin console:
namespace ceph { 
namespace osd_cmds { 

int heap(CephContext& cct, const cmdmap_t& cmdmap, Formatter& f, std::ostream& os);
 
}} // namespace ceph::osd_cmds

int OSD::mkfs(CephContext *cct, ObjectStore *store, uuid_d fsid, int whoami)
{
  int ret;

  OSDSuperblock sb;
  bufferlist sbbl;
  ObjectStore::CollectionHandle ch;

  // if we are fed a uuid for this osd, use it.
  store->set_fsid(cct->_conf->osd_uuid);

  ret = store->mkfs();
  if (ret) {
    derr << "OSD::mkfs: ObjectStore::mkfs failed with error "
         << cpp_strerror(ret) << dendl;
    goto free_store;
  }

  store->set_cache_shards(1);  // doesn't matter for mkfs!

  ret = store->mount();
  if (ret) {
    derr << "OSD::mkfs: couldn't mount ObjectStore: error "
         << cpp_strerror(ret) << dendl;
    goto free_store;
  }

  ch = store->open_collection(coll_t::meta());
  if (ch) {
    ret = store->read(ch, OSD_SUPERBLOCK_GOBJECT, 0, 0, sbbl);
    if (ret < 0) {
      derr << "OSD::mkfs: have meta collection but no superblock" << dendl;
      goto free_store;
    }
    /* if we already have superblock, check content of superblock */
    dout(0) << " have superblock" << dendl;
    auto p = sbbl.cbegin();
    decode(sb, p);
    if (whoami != sb.whoami) {
      derr << "provided osd id " << whoami << " != superblock's " << sb.whoami
	   << dendl;
      ret = -EINVAL;
      goto umount_store;
    }
    if (fsid != sb.cluster_fsid) {
      derr << "provided cluster fsid " << fsid
	   << " != superblock's " << sb.cluster_fsid << dendl;
      ret = -EINVAL;
      goto umount_store;
    }
  } else {
    // create superblock
    sb.cluster_fsid = fsid;
    sb.osd_fsid = store->get_fsid();
    sb.whoami = whoami;
    sb.compat_features = get_osd_initial_compat_set();

    bufferlist bl;
    encode(sb, bl);

    ObjectStore::CollectionHandle ch = store->create_new_collection(
      coll_t::meta());
    ObjectStore::Transaction t;
    t.create_collection(coll_t::meta(), 0);
    t.write(coll_t::meta(), OSD_SUPERBLOCK_GOBJECT, 0, bl.length(), bl);
    ret = store->queue_transaction(ch, std::move(t));
    if (ret) {
      derr << "OSD::mkfs: error while writing OSD_SUPERBLOCK_GOBJECT: "
	   << "queue_transaction returned " << cpp_strerror(ret) << dendl;
      goto umount_store;
    }
  }

  ret = write_meta(cct, store, sb.cluster_fsid, sb.osd_fsid, whoami);
  if (ret) {
    derr << "OSD::mkfs: failed to write fsid file: error "
         << cpp_strerror(ret) << dendl;
    goto umount_store;
  }

umount_store:
  if (ch) {
    ch.reset();
  }
  store->umount();
free_store:
  delete store;
  return ret;
}

int OSD::write_meta(CephContext *cct, ObjectStore *store, uuid_d& cluster_fsid, uuid_d& osd_fsid, int whoami)
{
  char val[80];
  int r;

  snprintf(val, sizeof(val), "%s", CEPH_OSD_ONDISK_MAGIC);
  r = store->write_meta("magic", val);
  if (r < 0)
    return r;

  snprintf(val, sizeof(val), "%d", whoami);
  r = store->write_meta("whoami", val);
  if (r < 0)
    return r;

  cluster_fsid.print(val);
  r = store->write_meta("ceph_fsid", val);
  if (r < 0)
    return r;

  string key = cct->_conf.get_val<string>("key");
  if (key.size()) {
    r = store->write_meta("osd_key", key);
    if (r < 0)
      return r;
  } else {
    string keyfile = cct->_conf.get_val<string>("keyfile");
    if (!keyfile.empty()) {
      bufferlist keybl;
      string err;
      r = keybl.read_file(keyfile.c_str(), &err);
      if (r < 0) {
	derr << __func__ << " failed to read keyfile " << keyfile << ": "
	     << err << ": " << cpp_strerror(r) << dendl;
	return r;
      }
      r = store->write_meta("osd_key", keybl.to_str());
      if (r < 0)
	return r;
    }
  }

  r = store->write_meta("ready", "ready");
  if (r < 0)
    return r;

  return 0;
}

int OSD::peek_meta(ObjectStore *store,
		   std::string *magic,
		   uuid_d *cluster_fsid,
		   uuid_d *osd_fsid,
		   int *whoami,
		   ceph_release_t *require_osd_release)
{
  string val;

  int r = store->read_meta("magic", &val);
  if (r < 0)
    return r;
  *magic = val;

  r = store->read_meta("whoami", &val);
  if (r < 0)
    return r;
  *whoami = atoi(val.c_str());

  r = store->read_meta("ceph_fsid", &val);
  if (r < 0)
    return r;
  r = cluster_fsid->parse(val.c_str());
  if (!r)
    return -EINVAL;

  r = store->read_meta("fsid", &val);
  if (r < 0) {
    *osd_fsid = uuid_d();
  } else {
    r = osd_fsid->parse(val.c_str());
    if (!r)
      return -EINVAL;
  }

  r = store->read_meta("require_osd_release", &val);
  if (r >= 0) {
    *require_osd_release = ceph_release_from_name(val);
  }

  return 0;
}


#undef dout_prefix
#define dout_prefix _prefix(_dout, whoami, get_osdmap_epoch())

// cons/des

OSD::OSD(CephContext *cct_, ObjectStore *store_,
	 int id,
	 Messenger *internal_messenger,
	 Messenger *external_messenger,
	 Messenger *hb_client_front,
	 Messenger *hb_client_back,
	 Messenger *hb_front_serverm,
	 Messenger *hb_back_serverm,
	 Messenger *osdc_messenger,
	 MonClient *mc,
	 const std::string &dev, const std::string &jdev) :
  Dispatcher(cct_),
  osd_lock("OSD::osd_lock"),
  tick_timer(cct, osd_lock),
  tick_timer_lock("OSD::tick_timer_lock"),
  tick_timer_without_osd_lock(cct, tick_timer_lock),
  gss_ktfile_client(cct->_conf.get_val<std::string>("gss_ktab_client_file")),
  cluster_messenger(internal_messenger),
  client_messenger(external_messenger),
  objecter_messenger(osdc_messenger),
  monc(mc),
  mgrc(cct_, client_messenger),
  logger(NULL),
  recoverystate_perf(NULL),
  store(store_),
  log_client(cct, client_messenger, &mc->monmap, LogClient::NO_FLAGS),
  clog(log_client.create_channel()),
  whoami(id),
  dev_path(dev), journal_path(jdev),
  store_is_rotational(store->is_rotational()),
  trace_endpoint("0.0.0.0", 0, "osd"),
  asok_hook(NULL),
  m_osd_pg_epoch_max_lag_factor(cct->_conf.get_val<double>(
				  "osd_pg_epoch_max_lag_factor")),
  osd_compat(get_osd_compat_set()),
  osd_op_tp(cct, "OSD::osd_op_tp", "tp_osd_tp",
	    get_num_op_threads()),
  command_tp(cct, "OSD::command_tp", "tp_osd_cmd",  1),
  session_waiting_lock("OSD::session_waiting_lock"),
  osdmap_subscribe_lock("OSD::osdmap_subscribe_lock"),
  heartbeat_lock("OSD::heartbeat_lock"),
  heartbeat_stop(false),
  heartbeat_need_update(true),
  hb_front_client_messenger(hb_client_front),
  hb_back_client_messenger(hb_client_back),
  hb_front_server_messenger(hb_front_serverm),
  hb_back_server_messenger(hb_back_serverm),
  daily_loadavg(0.0),
  heartbeat_thread(this),
  heartbeat_dispatcher(this),
  op_tracker(cct, cct->_conf->osd_enable_op_tracker,
                  cct->_conf->osd_num_op_tracker_shard),
  test_ops_hook(NULL),
  op_queue(get_io_queue()),
  op_prio_cutoff(get_io_prio_cut()),
  op_shardedwq(
    this,
    cct->_conf->osd_op_thread_timeout,
    cct->_conf->osd_op_thread_suicide_timeout,
    &osd_op_tp),
  map_lock("OSD::map_lock"),
  last_pg_create_epoch(0),
  mon_report_lock("OSD::mon_report_lock"),
  boot_finisher(cct),
  up_thru_wanted(0),
  requested_full_first(0),
  requested_full_last(0),
  command_wq(
    this,
    cct->_conf->osd_command_thread_timeout,
    cct->_conf->osd_command_thread_suicide_timeout,
    &command_tp),
  service(this)
{

  if (!gss_ktfile_client.empty()) {
    // Assert we can export environment variable 
    /* 
        The default client keytab is used, if it is present and readable,
        to automatically obtain initial credentials for GSSAPI client
        applications. The principal name of the first entry in the client
        keytab is used by default when obtaining initial credentials.
        1. The KRB5_CLIENT_KTNAME environment variable.
        2. The default_client_keytab_name profile variable in [libdefaults].
        3. The hardcoded default, DEFCKTNAME.
    */
    const int32_t set_result(setenv("KRB5_CLIENT_KTNAME", 
                                    gss_ktfile_client.c_str(), 1));
    ceph_assert(set_result == 0);
  }

  monc->set_messenger(client_messenger);
  op_tracker.set_complaint_and_threshold(cct->_conf->osd_op_complaint_time,
                                         cct->_conf->osd_op_log_threshold);
  op_tracker.set_history_size_and_duration(cct->_conf->osd_op_history_size,
                                           cct->_conf->osd_op_history_duration);
  op_tracker.set_history_slow_op_size_and_threshold(cct->_conf->osd_op_history_slow_op_size,
                                                    cct->_conf->osd_op_history_slow_op_threshold);
  ObjectCleanRegions::set_max_num_intervals(cct->_conf->osd_object_clean_region_max_num_intervals);
#ifdef WITH_BLKIN
  std::stringstream ss;
  ss << "osd." << whoami;
  trace_endpoint.copy_name(ss.str());
#endif

  // initialize shards
  num_shards = get_num_op_shards();
  for (uint32_t i = 0; i < num_shards; i++) {
    OSDShard *one_shard = new OSDShard(
      i,
      cct,
      this,
      cct->_conf->osd_op_pq_max_tokens_per_priority,
      cct->_conf->osd_op_pq_min_cost,
      op_queue);
    shards.push_back(one_shard);
  }
}

OSD::~OSD()
{
  while (!shards.empty()) {
    delete shards.back();
    shards.pop_back();
  }
  delete class_handler;
  cct->get_perfcounters_collection()->remove(recoverystate_perf);
  cct->get_perfcounters_collection()->remove(logger);
  delete recoverystate_perf;
  delete logger;
  delete store;
}

double OSD::get_tick_interval() const
{
  // vary +/- 5% to avoid scrub scheduling livelocks
  constexpr auto delta = 0.05;
  return (OSD_TICK_INTERVAL *
	  ceph::util::generate_random_number(1.0 - delta, 1.0 + delta));
}

void cls_initialize(ClassHandler *ch);

void OSD::handle_signal(int signum)
{
  ceph_assert(signum == SIGINT || signum == SIGTERM);
  derr << "*** Got signal " << sig_str(signum) << " ***" << dendl;
  shutdown();
}

int OSD::pre_init()
{
  std::lock_guard lock(osd_lock);
  if (is_stopping())
    return 0;

  if (store->test_mount_in_use()) {
    derr << "OSD::pre_init: object store '" << dev_path << "' is "
         << "currently in use. (Is ceph-osd already running?)" << dendl;
    return -EBUSY;
  }

  cct->_conf.add_observer(this);
  return 0;
}

int OSD::set_numa_affinity()
{
  // storage numa node
  int store_node = -1;
  store->get_numa_node(&store_node, nullptr, nullptr);
  if (store_node >= 0) {
    dout(1) << __func__ << " storage numa node " << store_node << dendl;
  }

  // check network numa node(s)
  int front_node = -1, back_node = -1;
  string front_iface = pick_iface(
    cct,
    client_messenger->get_myaddrs().front().get_sockaddr_storage());
  string back_iface = pick_iface(
    cct,
    cluster_messenger->get_myaddrs().front().get_sockaddr_storage());
  int r = get_iface_numa_node(front_iface, &front_node);
  if (r >= 0) {
    dout(1) << __func__ << " public network " << front_iface << " numa node "
	      << front_node << dendl;
    r = get_iface_numa_node(back_iface, &back_node);
    if (r >= 0) {
      dout(1) << __func__ << " cluster network " << back_iface << " numa node "
	      << back_node << dendl;
      if (front_node == back_node &&
	  front_node == store_node) {
	dout(1) << " objectstore and network numa nodes all match" << dendl;
	if (g_conf().get_val<bool>("osd_numa_auto_affinity")) {
	  numa_node = front_node;
	}
      } else {
	dout(1) << __func__ << " objectstore and network numa nodes do not match"
		<< dendl;
      }
    }
  } else {
    derr << __func__ << " unable to identify public interface '" << front_iface
	 << "' numa node: " << cpp_strerror(r) << dendl;
  }
  if (int node = g_conf().get_val<int64_t>("osd_numa_node"); node >= 0) {
    // this takes precedence over the automagic logic above
    numa_node = node;
  }
  if (numa_node >= 0) {
    int r = get_numa_node_cpu_set(numa_node, &numa_cpu_set_size, &numa_cpu_set);
    if (r < 0) {
      dout(1) << __func__ << " unable to determine numa node " << numa_node
	      << " CPUs" << dendl;
      numa_node = -1;
    } else {
      dout(1) << __func__ << " setting numa affinity to node " << numa_node
	      << " cpus "
	      << cpu_set_to_str_list(numa_cpu_set_size, &numa_cpu_set)
	      << dendl;
      r = sched_setaffinity(getpid(), numa_cpu_set_size, &numa_cpu_set);
      if (r < 0) {
	r = -errno;
	derr << __func__ << " failed to set numa affinity: " << cpp_strerror(r)
	     << dendl;
	numa_node = -1;
      }
    }
  } else {
    dout(1) << __func__ << " not setting numa affinity" << dendl;
  }
  return 0;
}

// asok

class OSDSocketHook : public AdminSocketHook {
  OSD *osd;
public:
  explicit OSDSocketHook(OSD *o) : osd(o) {}
  bool call(std::string_view admin_command, const cmdmap_t& cmdmap,
	    std::string_view format, bufferlist& out) override {
    stringstream ss;
    bool r = true;
    try {
      r = osd->asok_command(admin_command, cmdmap, format, ss);
    } catch (const bad_cmd_get& e) {
      ss << e.what();
      r = true;
    }
    out.append(ss);
    return r;
  }
};

std::set<int64_t> OSD::get_mapped_pools()
{
  std::set<int64_t> pools;
  std::vector<spg_t> pgids;
  _get_pgids(&pgids);
  for (const auto &pgid : pgids) {
    pools.insert(pgid.pool());
  }
  return pools;
}

bool OSD::asok_command(std::string_view admin_command, const cmdmap_t& cmdmap,
		       std::string_view format, ostream& ss)
{
  Formatter *f = Formatter::create(format, "json-pretty", "json-pretty");
  if (admin_command == "status") {
    f->open_object_section("status");
    f->dump_stream("cluster_fsid") << superblock.cluster_fsid;
    f->dump_stream("osd_fsid") << superblock.osd_fsid;
    f->dump_unsigned("whoami", superblock.whoami);
    f->dump_string("state", get_state_name(get_state()));
    f->dump_unsigned("oldest_map", superblock.oldest_map);
    f->dump_unsigned("newest_map", superblock.newest_map);
    f->dump_unsigned("num_pgs", num_pgs);
    f->close_section();
  } else if (admin_command == "flush_journal") {
    store->flush_journal();
  } else if (admin_command == "dump_ops_in_flight" ||
             admin_command == "ops" ||
             admin_command == "dump_blocked_ops" ||
             admin_command == "dump_historic_ops" ||
             admin_command == "dump_historic_ops_by_duration" ||
             admin_command == "dump_historic_slow_ops") {

    const string error_str = "op_tracker tracking is not enabled now, so no ops are tracked currently, \
even those get stuck. Please enable \"osd_enable_op_tracker\", and the tracker \
will start to track new ops received afterwards.";

    set<string> filters;
    vector<string> filter_str;
    if (cmd_getval(cct, cmdmap, "filterstr", filter_str)) {
        copy(filter_str.begin(), filter_str.end(),
           inserter(filters, filters.end()));
    }

    if (admin_command == "dump_ops_in_flight" ||
        admin_command == "ops") {
      if (!op_tracker.dump_ops_in_flight(f, false, filters)) {
        ss << error_str;
      }
    }
    if (admin_command == "dump_blocked_ops") {
      if (!op_tracker.dump_ops_in_flight(f, true, filters)) {
        ss << error_str;
      }
    }
    if (admin_command == "dump_historic_ops") {
      if (!op_tracker.dump_historic_ops(f, false, filters)) {
        ss << error_str;
      }
    }
    if (admin_command == "dump_historic_ops_by_duration") {
      if (!op_tracker.dump_historic_ops(f, true, filters)) {
        ss << error_str;
      }
    }
    if (admin_command == "dump_historic_slow_ops") {
      if (!op_tracker.dump_historic_slow_ops(f, filters)) {
        ss << error_str;
      }
    }
  } else if (admin_command == "dump_op_pq_state") {
    f->open_object_section("pq");
    op_shardedwq.dump(f);
    f->close_section();
  } else if (admin_command == "dump_blacklist") {
    list<pair<entity_addr_t,utime_t> > bl;
    OSDMapRef curmap = service.get_osdmap();

    f->open_array_section("blacklist");
    curmap->get_blacklist(&bl);
    for (list<pair<entity_addr_t,utime_t> >::iterator it = bl.begin();
	it != bl.end(); ++it) {
      f->open_object_section("entry");
      f->open_object_section("entity_addr_t");
      it->first.dump(f);
      f->close_section(); //entity_addr_t
      it->second.localtime(f->dump_stream("expire_time"));
      f->close_section(); //entry
    }
    f->close_section(); //blacklist
  } else if (admin_command == "dump_watchers") {
    list<obj_watch_item_t> watchers;
    // scan pg's
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto& pg : pgs) {
      list<obj_watch_item_t> pg_watchers;
      pg->get_watchers(&pg_watchers);
      watchers.splice(watchers.end(), pg_watchers);
    }

    f->open_array_section("watchers");
    for (list<obj_watch_item_t>::iterator it = watchers.begin();
	it != watchers.end(); ++it) {

      f->open_object_section("watch");

      f->dump_string("namespace", it->obj.nspace);
      f->dump_string("object", it->obj.oid.name);

      f->open_object_section("entity_name");
      it->wi.name.dump(f);
      f->close_section(); //entity_name_t

      f->dump_unsigned("cookie", it->wi.cookie);
      f->dump_unsigned("timeout", it->wi.timeout_seconds);

      f->open_object_section("entity_addr_t");
      it->wi.addr.dump(f);
      f->close_section(); //entity_addr_t

      f->close_section(); //watch
    }

    f->close_section(); //watchers
  } else if (admin_command == "dump_reservations") {
    f->open_object_section("reservations");
    f->open_object_section("local_reservations");
    service.local_reserver.dump(f);
    f->close_section();
    f->open_object_section("remote_reservations");
    service.remote_reserver.dump(f);
    f->close_section();
    f->close_section();
  } else if (admin_command == "get_latest_osdmap") {
    get_latest_osdmap();
  } else if (admin_command == "heap") {
    auto result = ceph::osd_cmds::heap(*cct, cmdmap, *f, ss);

    // Note: Failed heap profile commands won't necessarily trigger an error:
    f->open_object_section("result");
    f->dump_string("error", cpp_strerror(result));
    f->dump_bool("success", result >= 0);
    f->close_section();
  } else if (admin_command == "set_heap_property") {
    string property;
    int64_t value = 0;
    string error;
    bool success = false;
    if (!cmd_getval(cct, cmdmap, "property", property)) {
      error = "unable to get property";
      success = false;
    } else if (!cmd_getval(cct, cmdmap, "value", value)) {
      error = "unable to get value";
      success = false;
    } else if (value < 0) {
      error = "negative value not allowed";
      success = false;
    } else if (!ceph_heap_set_numeric_property(property.c_str(), (size_t)value)) {
      error = "invalid property";
      success = false;
    } else {
      success = true;
    }
    f->open_object_section("result");
    f->dump_string("error", error);
    f->dump_bool("success", success);
    f->close_section();
  } else if (admin_command == "get_heap_property") {
    string property;
    size_t value = 0;
    string error;
    bool success = false;
    if (!cmd_getval(cct, cmdmap, "property", property)) {
      error = "unable to get property";
      success = false;
    } else if (!ceph_heap_get_numeric_property(property.c_str(), &value)) {
      error = "invalid property";
      success = false;
    } else {
      success = true;
    }
    f->open_object_section("result");
    f->dump_string("error", error);
    f->dump_bool("success", success);
    f->dump_int("value", value);
    f->close_section();
  } else if (admin_command == "dump_objectstore_kv_stats") {
    store->get_db_statistics(f);
  } else if (admin_command == "dump_scrubs") {
    service.dumps_scrub(f);
  } else if (admin_command == "calc_objectstore_db_histogram") {
    store->generate_db_histogram(f);
  } else if (admin_command == "flush_store_cache") {
    store->flush_cache(&ss);
  } else if (admin_command == "dump_pgstate_history") {
    f->open_object_section("pgstate_history");
    f->open_array_section("pgs");
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto& pg : pgs) {
      f->open_object_section("pg");
      f->dump_stream("pg") << pg->pg_id;
      f->dump_string("currently", pg->get_current_state());
      pg->dump_pgstate_history(f);
      f->close_section();
    }
    f->close_section();
    f->close_section();
  } else if (admin_command == "compact") {
    dout(1) << "triggering manual compaction" << dendl;
    auto start = ceph::coarse_mono_clock::now();
    store->compact();
    auto end = ceph::coarse_mono_clock::now();
    double duration = std::chrono::duration<double>(end-start).count();
    dout(1) << "finished manual compaction in " 
            << duration
            << " seconds" << dendl;
    f->open_object_section("compact_result");
    f->dump_float("elapsed_time", duration);
    f->close_section();
  } else if (admin_command == "get_mapped_pools") {
    f->open_array_section("mapped_pools");
    set<int64_t> poollist = get_mapped_pools();
    for (auto pool : poollist) {
      f->dump_int("pool_id", pool);
    }
    f->close_section();
  } else if (admin_command == "smart") {
    string devid;
    cmd_getval(cct, cmdmap, "devid", devid);
    probe_smart(devid, ss);
  } else if (admin_command == "list_devices") {
    set<string> devnames;
    store->get_devices(&devnames);
    f->open_object_section("list_devices");
    for (auto dev : devnames) {
      if (dev.find("dm-") == 0) {
	continue;
      }
      f->dump_string("device", "/dev/" + dev);
    }
    f->close_section();
  } else if (admin_command == "send_beacon") {
    if (is_active()) {
      send_beacon(ceph::coarse_mono_clock::now());
    }
  } else {
    ceph_abort_msg("broken asok registration");
  }
  f->flush(ss);
  delete f;
  return true;
}

class TestOpsSocketHook : public AdminSocketHook {
  OSDService *service;
  ObjectStore *store;
public:
  TestOpsSocketHook(OSDService *s, ObjectStore *st) : service(s), store(st) {}
  bool call(std::string_view command, const cmdmap_t& cmdmap,
	    std::string_view format, bufferlist& out) override {
    stringstream ss;
    try {
      test_ops(service, store, command, cmdmap, ss);
    } catch (const bad_cmd_get& e) {
      ss << e.what();
    }
    out.append(ss);
    return true;
  }
  void test_ops(OSDService *service, ObjectStore *store,
		std::string_view command, const cmdmap_t& cmdmap, ostream &ss);

};

class OSD::C_Tick : public Context {
  OSD *osd;
  public:
  explicit C_Tick(OSD *o) : osd(o) {}
  void finish(int r) override {
    osd->tick();
  }
};

class OSD::C_Tick_WithoutOSDLock : public Context {
  OSD *osd;
  public:
  explicit C_Tick_WithoutOSDLock(OSD *o) : osd(o) {}
  void finish(int r) override {
    osd->tick_without_osd_lock();
  }
};

int OSD::enable_disable_fuse(bool stop)
{
#ifdef HAVE_LIBFUSE
  int r;
  string mntpath = cct->_conf->osd_data + "/fuse";
  if (fuse_store && (stop || !cct->_conf->osd_objectstore_fuse)) {
    dout(1) << __func__ << " disabling" << dendl;
    fuse_store->stop();
    delete fuse_store;
    fuse_store = NULL;
    r = ::rmdir(mntpath.c_str());
    if (r < 0) {
      r = -errno;
      derr << __func__ << " failed to rmdir " << mntpath << ": "
           << cpp_strerror(r) << dendl;
      return r;
    }
    return 0;
  }
  if (!fuse_store && cct->_conf->osd_objectstore_fuse) {
    dout(1) << __func__ << " enabling" << dendl;
    r = ::mkdir(mntpath.c_str(), 0700);
    if (r < 0)
      r = -errno;
    if (r < 0 && r != -EEXIST) {
      derr << __func__ << " unable to create " << mntpath << ": "
	   << cpp_strerror(r) << dendl;
      return r;
    }
    fuse_store = new FuseStore(store, mntpath);
    r = fuse_store->start();
    if (r < 0) {
      derr << __func__ << " unable to start fuse: " << cpp_strerror(r) << dendl;
      delete fuse_store;
      fuse_store = NULL;
      return r;
    }
  }
#endif  // HAVE_LIBFUSE
  return 0;
}

int OSD::get_num_op_shards()
{
  if (cct->_conf->osd_op_num_shards)
    return cct->_conf->osd_op_num_shards;
  if (store_is_rotational)
    return cct->_conf->osd_op_num_shards_hdd;
  else
    return cct->_conf->osd_op_num_shards_ssd;
}

int OSD::get_num_op_threads()
{
  if (cct->_conf->osd_op_num_threads_per_shard)
    return get_num_op_shards() * cct->_conf->osd_op_num_threads_per_shard;
  if (store_is_rotational)
    return get_num_op_shards() * cct->_conf->osd_op_num_threads_per_shard_hdd;
  else
    return get_num_op_shards() * cct->_conf->osd_op_num_threads_per_shard_ssd;
}

float OSD::get_osd_recovery_sleep()
{
  if (cct->_conf->osd_recovery_sleep)
    return cct->_conf->osd_recovery_sleep;
  if (!store_is_rotational && !journal_is_rotational)
    return cct->_conf->osd_recovery_sleep_ssd;
  else if (store_is_rotational && !journal_is_rotational)
    return cct->_conf.get_val<double>("osd_recovery_sleep_hybrid");
  else
    return cct->_conf->osd_recovery_sleep_hdd;
}

float OSD::get_osd_delete_sleep()
{
  float osd_delete_sleep = cct->_conf.get_val<double>("osd_delete_sleep");
  if (osd_delete_sleep > 0)
    return osd_delete_sleep;
  if (!store_is_rotational && !journal_is_rotational)
    return cct->_conf.get_val<double>("osd_delete_sleep_ssd");
  if (store_is_rotational && !journal_is_rotational)
    return cct->_conf.get_val<double>("osd_delete_sleep_hybrid");
  return cct->_conf.get_val<double>("osd_delete_sleep_hdd");
}

int OSD::get_recovery_max_active()
{
  if (cct->_conf->osd_recovery_max_active)
    return cct->_conf->osd_recovery_max_active;
  if (store_is_rotational)
    return cct->_conf->osd_recovery_max_active_hdd;
  else
    return cct->_conf->osd_recovery_max_active_ssd;
}

int OSD::init()
{
  CompatSet initial, diff;
  std::lock_guard lock(osd_lock);
  if (is_stopping())
    return 0;

  tick_timer.init();
  tick_timer_without_osd_lock.init();
  service.recovery_request_timer.init();
  service.sleep_timer.init();

  boot_finisher.start();

  {
    string val;
    store->read_meta("require_osd_release", &val);
    last_require_osd_release = ceph_release_from_name(val);
  }

  // mount.
  dout(2) << "init " << dev_path
	  << " (looks like " << (store_is_rotational ? "hdd" : "ssd") << ")"
	  << dendl;
  dout(2) << "journal " << journal_path << dendl;
  ceph_assert(store);  // call pre_init() first!

  store->set_cache_shards(get_num_op_shards());

  int r = store->mount();
  if (r < 0) {
    derr << "OSD:init: unable to mount object store" << dendl;
    return r;
  }
  journal_is_rotational = store->is_journal_rotational();
  dout(2) << "journal looks like " << (journal_is_rotational ? "hdd" : "ssd")
          << dendl;

  enable_disable_fuse(false);

  dout(2) << "boot" << dendl;

  service.meta_ch = store->open_collection(coll_t::meta());

  // initialize the daily loadavg with current 15min loadavg
  double loadavgs[3];
  if (getloadavg(loadavgs, 3) == 3) {
    daily_loadavg = loadavgs[2];
  } else {
    derr << "OSD::init() : couldn't read loadavgs\n" << dendl;
    daily_loadavg = 1.0;
  }

  int rotating_auth_attempts = 0;
  auto rotating_auth_timeout =
    g_conf().get_val<int64_t>("rotating_keys_bootstrap_timeout");

  // sanity check long object name handling
  {
    hobject_t l;
    l.oid.name = string(cct->_conf->osd_max_object_name_len, 'n');
    l.set_key(string(cct->_conf->osd_max_object_name_len, 'k'));
    l.nspace = string(cct->_conf->osd_max_object_namespace_len, 's');
    r = store->validate_hobject_key(l);
    if (r < 0) {
      derr << "backend (" << store->get_type() << ") is unable to support max "
	   << "object name[space] len" << dendl;
      derr << "   osd max object name len = "
	   << cct->_conf->osd_max_object_name_len << dendl;
      derr << "   osd max object namespace len = "
	   << cct->_conf->osd_max_object_namespace_len << dendl;
      derr << cpp_strerror(r) << dendl;
      if (cct->_conf->osd_check_max_object_name_len_on_startup) {
	goto out;
      }
      derr << "osd_check_max_object_name_len_on_startup = false, starting anyway"
	   << dendl;
    } else {
      dout(20) << "configured osd_max_object_name[space]_len looks ok" << dendl;
    }
  }

  // read superblock
  r = read_superblock();
  if (r < 0) {
    derr << "OSD::init() : unable to read osd superblock" << dendl;
    r = -EINVAL;
    goto out;
  }

  if (osd_compat.compare(superblock.compat_features) < 0) {
    derr << "The disk uses features unsupported by the executable." << dendl;
    derr << " ondisk features " << superblock.compat_features << dendl;
    derr << " daemon features " << osd_compat << dendl;

    if (osd_compat.writeable(superblock.compat_features)) {
      CompatSet diff = osd_compat.unsupported(superblock.compat_features);
      derr << "it is still writeable, though. Missing features: " << diff << dendl;
      r = -EOPNOTSUPP;
      goto out;
    }
    else {
      CompatSet diff = osd_compat.unsupported(superblock.compat_features);
      derr << "Cannot write to disk! Missing features: " << diff << dendl;
      r = -EOPNOTSUPP;
      goto out;
    }
  }

  assert_warn(whoami == superblock.whoami);
  if (whoami != superblock.whoami) {
    derr << "OSD::init: superblock says osd"
	 << superblock.whoami << " but I am osd." << whoami << dendl;
    r = -EINVAL;
    goto out;
  }

  // load up "current" osdmap
  assert_warn(!osdmap);
  if (osdmap) {
    derr << "OSD::init: unable to read current osdmap" << dendl;
    r = -EINVAL;
    goto out;
  }
  osdmap = get_map(superblock.current_epoch);

  // make sure we don't have legacy pgs deleting
  {
    vector<coll_t> ls;
    int r = store->list_collections(ls);
    ceph_assert(r >= 0);
    for (auto c : ls) {
      spg_t pgid;
      if (c.is_pg(&pgid) &&
	  !osdmap->have_pg_pool(pgid.pool())) {
	ghobject_t oid = make_final_pool_info_oid(pgid.pool());
	if (!store->exists(service.meta_ch, oid)) {
	  derr << __func__ << " missing pg_pool_t for deleted pool "
	       << pgid.pool() << " for pg " << pgid
	       << "; please downgrade to luminous and allow "
	       << "pg deletion to complete before upgrading" << dendl;
	  ceph_abort();
	}
      }
    }
  }

  initial = get_osd_initial_compat_set();
  diff = superblock.compat_features.unsupported(initial);
  if (superblock.compat_features.merge(initial)) {
    // Are we adding SNAPMAPPER2?
    if (diff.incompat.contains(CEPH_OSD_FEATURE_INCOMPAT_SNAPMAPPER2)) {
      dout(1) << __func__ << " upgrade snap_mapper (first start as octopus)"
	      << dendl;
      auto ch = service.meta_ch;
      auto hoid = make_snapmapper_oid();
      unsigned max = cct->_conf->osd_target_transaction_size;
      r = SnapMapper::convert_legacy(cct, store, ch, hoid, max);
      if (r < 0)
	goto out;
    }
    // We need to persist the new compat_set before we
    // do anything else
    dout(5) << "Upgrading superblock adding: " << diff << dendl;
    ObjectStore::Transaction t;
    write_superblock(t);
    r = store->queue_transaction(service.meta_ch, std::move(t));
    if (r < 0)
      goto out;
  }

  // make sure snap mapper object exists
  if (!store->exists(service.meta_ch, OSD::make_snapmapper_oid())) {
    dout(10) << "init creating/touching snapmapper object" << dendl;
    ObjectStore::Transaction t;
    t.touch(coll_t::meta(), OSD::make_snapmapper_oid());
    r = store->queue_transaction(service.meta_ch, std::move(t));
    if (r < 0)
      goto out;
  }
  if (!store->exists(service.meta_ch, OSD::make_purged_snaps_oid())) {
    dout(10) << "init creating/touching purged_snaps object" << dendl;
    ObjectStore::Transaction t;
    t.touch(coll_t::meta(), OSD::make_purged_snaps_oid());
    r = store->queue_transaction(service.meta_ch, std::move(t));
    if (r < 0)
      goto out;
  }

  class_handler = new ClassHandler(cct);
  cls_initialize(class_handler);

  if (cct->_conf->osd_open_classes_on_start) {
    int r = class_handler->open_all_classes();
    if (r)
      dout(1) << "warning: got an error loading one or more classes: " << cpp_strerror(r) << dendl;
  }

  check_osdmap_features();

  create_recoverystate_perf();

  {
    epoch_t bind_epoch = osdmap->get_epoch();
    service.set_epochs(NULL, NULL, &bind_epoch);
  }

  clear_temp_objects();

  // initialize osdmap references in sharded wq
  for (auto& shard : shards) {
    std::lock_guard l(shard->osdmap_lock);
    shard->shard_osdmap = osdmap;
  }

  // load up pgs (as they previously existed)
  load_pgs();

  dout(2) << "superblock: I am osd." << superblock.whoami << dendl;
  dout(0) << "using " << op_queue << " op queue with priority op cut off at " <<
    op_prio_cutoff << "." << dendl;

  create_logger();

  // prime osd stats
  {
    struct store_statfs_t stbuf;
    osd_alert_list_t alerts;
    int r = store->statfs(&stbuf, &alerts);
    ceph_assert(r == 0);
    service.set_statfs(stbuf, alerts);
  }

  // client_messenger auth_client is already set up by monc.
  for (auto m : { cluster_messenger,
	objecter_messenger,
	hb_front_client_messenger,
	hb_back_client_messenger,
	hb_front_server_messenger,
	hb_back_server_messenger } ) {
    m->set_auth_client(monc);
  }
  for (auto m : { client_messenger,
	cluster_messenger,
	hb_front_server_messenger,
	hb_back_server_messenger }) {
    m->set_auth_server(monc);
  }
  monc->set_handle_authentication_dispatcher(this);

  monc->set_want_keys(CEPH_ENTITY_TYPE_MON | CEPH_ENTITY_TYPE_OSD
                      | CEPH_ENTITY_TYPE_MGR);
  r = monc->init();
  if (r < 0)
    goto out;

  mgrc.set_pgstats_cb([this](){ return collect_pg_stats(); });
  mgrc.set_perf_metric_query_cb(
    [this](const std::map<OSDPerfMetricQuery, OSDPerfMetricLimits> &queries) {
        set_perf_queries(queries);
      },
      [this](std::map<OSDPerfMetricQuery, OSDPerfMetricReport> *reports) {
        get_perf_reports(reports);
      });
  mgrc.init();

  // tell monc about log_client so it will know about mon session resets
  monc->set_log_client(&log_client);
  update_log_config();

  // i'm ready!
  client_messenger->add_dispatcher_tail(&mgrc);
  client_messenger->add_dispatcher_tail(this);
  cluster_messenger->add_dispatcher_head(this);

  hb_front_client_messenger->add_dispatcher_head(&heartbeat_dispatcher);
  hb_back_client_messenger->add_dispatcher_head(&heartbeat_dispatcher);
  hb_front_server_messenger->add_dispatcher_head(&heartbeat_dispatcher);
  hb_back_server_messenger->add_dispatcher_head(&heartbeat_dispatcher);

  objecter_messenger->add_dispatcher_head(service.objecter);

  service.init();
  service.publish_map(osdmap);
  service.publish_superblock(superblock);
  service.max_oldest_map = superblock.oldest_map;

  for (auto& shard : shards) {
    // put PGs in a temporary set because we may modify pg_slots
    // unordered_map below.
    set<PGRef> pgs;
    for (auto& i : shard->pg_slots) {
      PGRef pg = i.second->pg;
      if (!pg) {
	continue;
      }
      pgs.insert(pg);
    }
    for (auto pg : pgs) {
      pg->lock();
      set<pair<spg_t,epoch_t>> new_children;
      set<pair<spg_t,epoch_t>> merge_pgs;
      service.identify_splits_and_merges(pg->get_osdmap(), osdmap, pg->pg_id,
					 &new_children, &merge_pgs);
      if (!new_children.empty()) {
	for (auto shard : shards) {
	  shard->prime_splits(osdmap, &new_children);
	}
	assert(new_children.empty());
      }
      if (!merge_pgs.empty()) {
	for (auto shard : shards) {
	  shard->prime_merges(osdmap, &merge_pgs);
	}
	assert(merge_pgs.empty());
      }
      pg->unlock();
    }
  }

  osd_op_tp.start();
  command_tp.start();

  // start the heartbeat
  heartbeat_thread.create("osd_srv_heartbt");

  // tick
  tick_timer.add_event_after(get_tick_interval(),
			     new C_Tick(this));
  {
    std::lock_guard l(tick_timer_lock);
    tick_timer_without_osd_lock.add_event_after(get_tick_interval(),
						new C_Tick_WithoutOSDLock(this));
  }

  osd_lock.Unlock();

  r = monc->authenticate();
  if (r < 0) {
    derr << __func__ << " authentication failed: " << cpp_strerror(r)
         << dendl;
    exit(1);
  }

  while (monc->wait_auth_rotating(rotating_auth_timeout) < 0) {
    derr << "unable to obtain rotating service keys; retrying" << dendl;
    ++rotating_auth_attempts;
    if (rotating_auth_attempts > g_conf()->max_rotating_auth_attempts) {
        derr << __func__ << " wait_auth_rotating timed out" << dendl;
	exit(1);
    }
  }

  r = update_crush_device_class();
  if (r < 0) {
    derr << __func__ << " unable to update_crush_device_class: "
	 << cpp_strerror(r) << dendl;
    exit(1);
  }

  r = update_crush_location();
  if (r < 0) {
    derr << __func__ << " unable to update_crush_location: "
         << cpp_strerror(r) << dendl;
    exit(1);
  }

  osd_lock.Lock();
  if (is_stopping())
    return 0;

  // start objecter *after* we have authenticated, so that we don't ignore
  // the OSDMaps it requests.
  service.final_init();

  check_config();

  dout(10) << "ensuring pgs have consumed prior maps" << dendl;
  consume_map();

  dout(0) << "done with init, starting boot process" << dendl;

  // subscribe to any pg creations
  monc->sub_want("osd_pg_creates", last_pg_create_epoch, 0);

  // MgrClient needs this (it doesn't have MonClient reference itself)
  monc->sub_want("mgrmap", 0, 0);

  // we don't need to ask for an osdmap here; objecter will
  //monc->sub_want("osdmap", osdmap->get_epoch(), CEPH_SUBSCRIBE_ONETIME);

  monc->renew_subs();

  start_boot();

  return 0;

out:
  enable_disable_fuse(true);
  store->umount();
  delete store;
  store = NULL;
  return r;
}

void OSD::final_init()
{
  AdminSocket *admin_socket = cct->get_admin_socket();
  asok_hook = new OSDSocketHook(this);
  int r = admin_socket->register_command("status", "status", asok_hook,
					 "high-level status of OSD");
  ceph_assert(r == 0);
  r = admin_socket->register_command("flush_journal", "flush_journal",
                                     asok_hook,
                                     "flush the journal to permanent store");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_ops_in_flight",
				     "dump_ops_in_flight " \
				     "name=filterstr,type=CephString,n=N,req=false",
				     asok_hook,
				     "show the ops currently in flight");
  ceph_assert(r == 0);
  r = admin_socket->register_command("ops",
				     "ops " \
				     "name=filterstr,type=CephString,n=N,req=false",
				     asok_hook,
				     "show the ops currently in flight");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_blocked_ops",
				     "dump_blocked_ops " \
				     "name=filterstr,type=CephString,n=N,req=false",
				     asok_hook,
				     "show the blocked ops currently in flight");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_historic_ops",
                                     "dump_historic_ops " \
                                     "name=filterstr,type=CephString,n=N,req=false",
				     asok_hook,
				     "show recent ops");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_historic_slow_ops",
                                     "dump_historic_slow_ops " \
                                     "name=filterstr,type=CephString,n=N,req=false",
				     asok_hook,
				     "show slowest recent ops");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_historic_ops_by_duration",
                                     "dump_historic_ops_by_duration " \
                                     "name=filterstr,type=CephString,n=N,req=false",
				     asok_hook,
				     "show slowest recent ops, sorted by duration");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_op_pq_state", "dump_op_pq_state",
				     asok_hook,
				     "dump op priority queue state");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_blacklist", "dump_blacklist",
				     asok_hook,
				     "dump blacklisted clients and times");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_watchers", "dump_watchers",
				     asok_hook,
				     "show clients which have active watches,"
				     " and on which objects");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_reservations", "dump_reservations",
				     asok_hook,
				     "show recovery reservations");
  ceph_assert(r == 0);
  r = admin_socket->register_command("get_latest_osdmap", "get_latest_osdmap",
				     asok_hook,
				     "force osd to update the latest map from "
				     "the mon");
  ceph_assert(r == 0);

  r = admin_socket->register_command( "heap",
                                      "heap " \
                                      "name=heapcmd,type=CephString " \
                                      "name=value,type=CephString,req=false",
                                      asok_hook,
                                      "show heap usage info (available only if "
                                      "compiled with tcmalloc)");
  ceph_assert(r == 0);

  r = admin_socket->register_command("set_heap_property",
				     "set_heap_property " \
				     "name=property,type=CephString " \
				     "name=value,type=CephInt",
				     asok_hook,
				     "update malloc extension heap property");
  ceph_assert(r == 0);

  r = admin_socket->register_command("get_heap_property",
				     "get_heap_property " \
				     "name=property,type=CephString",
				     asok_hook,
				     "get malloc extension heap property");
  ceph_assert(r == 0);

  r = admin_socket->register_command("dump_objectstore_kv_stats",
				     "dump_objectstore_kv_stats",
				     asok_hook,
				     "print statistics of kvdb which used by bluestore");
  ceph_assert(r == 0);

  r = admin_socket->register_command("dump_scrubs",
				     "dump_scrubs",
				     asok_hook,
				     "print scheduled scrubs");
  ceph_assert(r == 0);

  r = admin_socket->register_command("calc_objectstore_db_histogram",
                                     "calc_objectstore_db_histogram",
                                     asok_hook,
                                     "Generate key value histogram of kvdb(rocksdb) which used by bluestore");
  ceph_assert(r == 0);

  r = admin_socket->register_command("flush_store_cache",
                                     "flush_store_cache",
                                     asok_hook,
                                     "Flush bluestore internal cache");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_pgstate_history", "dump_pgstate_history",
				     asok_hook,
				     "show recent state history");
  ceph_assert(r == 0);

  r = admin_socket->register_command("compact", "compact",
				     asok_hook,
				     "Commpact object store's omap."
                                     " WARNING: Compaction probably slows your requests");
  ceph_assert(r == 0);

  r = admin_socket->register_command("get_mapped_pools", "get_mapped_pools",
                                     asok_hook,
                                     "dump pools whose PG(s) are mapped to this OSD.");

  ceph_assert(r == 0);

  r = admin_socket->register_command("smart", "smart name=devid,type=CephString,req=False",
                                     asok_hook,
                                     "probe OSD devices for SMART data.");

  ceph_assert(r == 0);

  r = admin_socket->register_command("list_devices", "list_devices",
                                     asok_hook,
                                     "list OSD devices.");
  r = admin_socket->register_command("send_beacon", "send_beacon",
                                     asok_hook,
                                     "send OSD beacon to mon immediately");

  test_ops_hook = new TestOpsSocketHook(&(this->service), this->store);
  // Note: pools are CephString instead of CephPoolname because
  // these commands traditionally support both pool names and numbers
  r = admin_socket->register_command(
   "setomapval",
   "setomapval " \
   "name=pool,type=CephString " \
   "name=objname,type=CephObjectname " \
   "name=key,type=CephString "\
   "name=val,type=CephString",
   test_ops_hook,
   "set omap key");
  ceph_assert(r == 0);
  r = admin_socket->register_command(
    "rmomapkey",
    "rmomapkey " \
    "name=pool,type=CephString " \
    "name=objname,type=CephObjectname " \
    "name=key,type=CephString",
    test_ops_hook,
    "remove omap key");
  ceph_assert(r == 0);
  r = admin_socket->register_command(
    "setomapheader",
    "setomapheader " \
    "name=pool,type=CephString " \
    "name=objname,type=CephObjectname " \
    "name=header,type=CephString",
    test_ops_hook,
    "set omap header");
  ceph_assert(r == 0);

  r = admin_socket->register_command(
    "getomap",
    "getomap " \
    "name=pool,type=CephString " \
    "name=objname,type=CephObjectname",
    test_ops_hook,
    "output entire object map");
  ceph_assert(r == 0);

  r = admin_socket->register_command(
    "truncobj",
    "truncobj " \
    "name=pool,type=CephString " \
    "name=objname,type=CephObjectname " \
    "name=len,type=CephInt",
    test_ops_hook,
    "truncate object to length");
  ceph_assert(r == 0);

  r = admin_socket->register_command(
    "injectdataerr",
    "injectdataerr " \
    "name=pool,type=CephString " \
    "name=objname,type=CephObjectname " \
    "name=shardid,type=CephInt,req=false,range=0|255",
    test_ops_hook,
    "inject data error to an object");
  ceph_assert(r == 0);

  r = admin_socket->register_command(
    "injectmdataerr",
    "injectmdataerr " \
    "name=pool,type=CephString " \
    "name=objname,type=CephObjectname " \
    "name=shardid,type=CephInt,req=false,range=0|255",
    test_ops_hook,
    "inject metadata error to an object");
  ceph_assert(r == 0);
  r = admin_socket->register_command(
    "set_recovery_delay",
    "set_recovery_delay " \
    "name=utime,type=CephInt,req=false",
    test_ops_hook,
     "Delay osd recovery by specified seconds");
  ceph_assert(r == 0);
  r = admin_socket->register_command(
   "trigger_scrub",
   "trigger_scrub " \
   "name=pgid,type=CephString " \
   "name=time,type=CephInt,req=false",
   test_ops_hook,
   "Trigger a scheduled scrub ");
  ceph_assert(r == 0);
  r = admin_socket->register_command(
   "trigger_deep_scrub",
   "trigger_deep_scrub " \
   "name=pgid,type=CephString " \
   "name=time,type=CephInt,req=false",
   test_ops_hook,
   "Trigger a scheduled deep scrub ");
  ceph_assert(r == 0);
  r = admin_socket->register_command(
   "injectfull",
   "injectfull " \
   "name=type,type=CephString,req=false " \
   "name=count,type=CephInt,req=false ",
   test_ops_hook,
   "Inject a full disk (optional count times)");
  ceph_assert(r == 0);
}

void OSD::create_logger()
{
  dout(10) << "create_logger" << dendl;

  logger = build_osd_logger(cct);
  cct->get_perfcounters_collection()->add(logger);
}

void OSD::create_recoverystate_perf()
{
  dout(10) << "create_recoverystate_perf" << dendl;

  recoverystate_perf = build_recoverystate_perf(cct);
  cct->get_perfcounters_collection()->add(recoverystate_perf);
}

int OSD::shutdown()
{
  if (!service.prepare_to_stop())
    return 0; // already shutting down
  osd_lock.Lock();
  if (is_stopping()) {
    osd_lock.Unlock();
    return 0;
  }
  dout(0) << "shutdown" << dendl;

  set_state(STATE_STOPPING);

  // Debugging
  if (cct->_conf.get_val<bool>("osd_debug_shutdown")) {
    cct->_conf.set_val("debug_osd", "100");
    cct->_conf.set_val("debug_journal", "100");
    cct->_conf.set_val("debug_filestore", "100");
    cct->_conf.set_val("debug_bluestore", "100");
    cct->_conf.set_val("debug_ms", "100");
    cct->_conf.apply_changes(nullptr);
  }

  // stop MgrClient earlier as it's more like an internal consumer of OSD
  mgrc.shutdown();

  service.start_shutdown();

  // stop sending work to pgs.  this just prevents any new work in _process
  // from racing with on_shutdown and potentially entering the pg after.
  op_shardedwq.drain();

  // Shutdown PGs
  {
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto pg : pgs) {
      pg->shutdown();
    }
  }

  // drain op queue again (in case PGs requeued something)
  op_shardedwq.drain();
  {
    finished.clear(); // zap waiters (bleh, this is messy)
    waiting_for_osdmap.clear();
  }

  // unregister commands
  cct->get_admin_socket()->unregister_commands(asok_hook);
  delete asok_hook;
  asok_hook = NULL;

  cct->get_admin_socket()->unregister_commands(test_ops_hook);
  delete test_ops_hook;
  test_ops_hook = NULL;

  osd_lock.Unlock();

  heartbeat_lock.Lock();
  heartbeat_stop = true;
  heartbeat_cond.Signal();
  heartbeat_lock.Unlock();
  heartbeat_thread.join();

  osd_op_tp.drain();
  osd_op_tp.stop();
  dout(10) << "op sharded tp stopped" << dendl;

  command_tp.drain();
  command_tp.stop();
  dout(10) << "command tp stopped" << dendl;

  dout(10) << "stopping agent" << dendl;
  service.agent_stop();

  boot_finisher.wait_for_empty();

  osd_lock.Lock();

  boot_finisher.stop();
  reset_heartbeat_peers(true);

  tick_timer.shutdown();

  {
    std::lock_guard l(tick_timer_lock);
    tick_timer_without_osd_lock.shutdown();
  }

  // note unmount epoch
  dout(10) << "noting clean unmount in epoch " << osdmap->get_epoch() << dendl;
  superblock.mounted = service.get_boot_epoch();
  superblock.clean_thru = osdmap->get_epoch();
  ObjectStore::Transaction t;
  write_superblock(t);
  int r = store->queue_transaction(service.meta_ch, std::move(t));
  if (r) {
    derr << "OSD::shutdown: error writing superblock: "
	 << cpp_strerror(r) << dendl;
  }


  service.shutdown_reserver();

  // Remove PGs
#ifdef PG_DEBUG_REFS
  service.dump_live_pgids();
#endif
  while (true) {
    vector<PGRef> pgs;
    _get_pgs(&pgs, true);
    if (pgs.empty()) {
      break;
    }
    for (auto& pg : pgs) {
      if (pg->is_deleted()) {
	continue;
      }
      dout(20) << " kicking pg " << pg << dendl;
      pg->lock();
      if (pg->get_num_ref() != 1) {
	derr << "pgid " << pg->get_pgid() << " has ref count of "
	     << pg->get_num_ref() << dendl;
#ifdef PG_DEBUG_REFS
	pg->dump_live_ids();
#endif
	if (cct->_conf->osd_shutdown_pgref_assert) {
	  ceph_abort();
	}
      }
      pg->ch.reset();
      pg->unlock();
    }
  }
#ifdef PG_DEBUG_REFS
  service.dump_live_pgids();
#endif

  osd_lock.Unlock();
  cct->_conf.remove_observer(this);
  osd_lock.Lock();

  service.meta_ch.reset();

  dout(10) << "syncing store" << dendl;
  enable_disable_fuse(true);

  if (cct->_conf->osd_journal_flush_on_shutdown) {
    dout(10) << "flushing journal" << dendl;
    store->flush_journal();
  }

  monc->shutdown();
  osd_lock.Unlock();

  map_lock.get_write();
  osdmap = OSDMapRef();
  map_lock.put_write();

  for (auto s : shards) {
    std::lock_guard l(s->osdmap_lock);
    s->shard_osdmap = OSDMapRef();
  }
  service.shutdown();

  std::lock_guard lock(osd_lock);
  store->umount();
  delete store;
  store = nullptr;
  dout(10) << "Store synced" << dendl;

  op_tracker.on_shutdown();

  class_handler->shutdown();
  client_messenger->shutdown();
  cluster_messenger->shutdown();
  hb_front_client_messenger->shutdown();
  hb_back_client_messenger->shutdown();
  objecter_messenger->shutdown();
  hb_front_server_messenger->shutdown();
  hb_back_server_messenger->shutdown();

  return r;
}

int OSD::mon_cmd_maybe_osd_create(string &cmd)
{
  bool created = false;
  while (true) {
    dout(10) << __func__ << " cmd: " << cmd << dendl;
    vector<string> vcmd{cmd};
    bufferlist inbl;
    C_SaferCond w;
    string outs;
    monc->start_mon_command(vcmd, inbl, NULL, &outs, &w);
    int r = w.wait();
    if (r < 0) {
      if (r == -ENOENT && !created) {
	string newcmd = "{\"prefix\": \"osd create\", \"id\": " + stringify(whoami)
	  + ", \"uuid\": \"" + stringify(superblock.osd_fsid) + "\"}";
	vector<string> vnewcmd{newcmd};
	bufferlist inbl;
	C_SaferCond w;
	string outs;
	monc->start_mon_command(vnewcmd, inbl, NULL, &outs, &w);
	int r = w.wait();
	if (r < 0) {
	  derr << __func__ << " fail: osd does not exist and created failed: "
	       << cpp_strerror(r) << dendl;
	  return r;
	}
	created = true;
	continue;
      }
      derr << __func__ << " fail: '" << outs << "': " << cpp_strerror(r) << dendl;
      return r;
    }
    break;
  }

  return 0;
}

int OSD::update_crush_location()
{
  if (!cct->_conf->osd_crush_update_on_start) {
    dout(10) << __func__ << " osd_crush_update_on_start = false" << dendl;
    return 0;
  }

  char weight[32];
  if (cct->_conf->osd_crush_initial_weight >= 0) {
    snprintf(weight, sizeof(weight), "%.4lf", cct->_conf->osd_crush_initial_weight);
  } else {
    struct store_statfs_t st;
    osd_alert_list_t alerts;
    int r = store->statfs(&st, &alerts);
    if (r < 0) {
      derr << "statfs: " << cpp_strerror(r) << dendl;
      return r;
    }
    snprintf(weight, sizeof(weight), "%.4lf",
	     std::max(.00001,
		      double(st.total) /
		      double(1ull << 40 /* TB */)));
  }

  dout(10) << __func__ << " crush location is " << cct->crush_location << dendl;

  string cmd =
    string("{\"prefix\": \"osd crush create-or-move\", ") +
    string("\"id\": ") + stringify(whoami) + ", " +
    string("\"weight\":") + weight + ", " +
    string("\"args\": [") + stringify(cct->crush_location) + "]}";
  return mon_cmd_maybe_osd_create(cmd);
}

int OSD::update_crush_device_class()
{
  if (!cct->_conf->osd_class_update_on_start) {
    dout(10) << __func__ << " osd_class_update_on_start = false" << dendl;
    return 0;
  }

  string device_class;
  int r = store->read_meta("crush_device_class", &device_class);
  if (r < 0 || device_class.empty()) {
    device_class = store->get_default_device_class();
  }

  if (device_class.empty()) {
    dout(20) << __func__ << " no device class stored locally" << dendl;
    return 0;
  }

  string cmd =
    string("{\"prefix\": \"osd crush set-device-class\", ") +
    string("\"class\": \"") + device_class + string("\", ") +
    string("\"ids\": [\"") + stringify(whoami) + string("\"]}");

  r = mon_cmd_maybe_osd_create(cmd);
  if (r == -EBUSY) {
    // good, already bound to a device-class
    return 0;
  } else {
    return r;
  }
}

void OSD::write_superblock(ObjectStore::Transaction& t)
{
  dout(10) << "write_superblock " << superblock << dendl;

  //hack: at minimum it's using the baseline feature set
  if (!superblock.compat_features.incompat.contains(CEPH_OSD_FEATURE_INCOMPAT_BASE))
    superblock.compat_features.incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_BASE);

  bufferlist bl;
  encode(superblock, bl);
  t.write(coll_t::meta(), OSD_SUPERBLOCK_GOBJECT, 0, bl.length(), bl);
}

int OSD::read_superblock()
{
  bufferlist bl;
  int r = store->read(service.meta_ch, OSD_SUPERBLOCK_GOBJECT, 0, 0, bl);
  if (r < 0)
    return r;

  auto p = bl.cbegin();
  decode(superblock, p);

  dout(10) << "read_superblock " << superblock << dendl;

  return 0;
}

void OSD::clear_temp_objects()
{
  dout(10) << __func__ << dendl;
  vector<coll_t> ls;
  store->list_collections(ls);
  for (vector<coll_t>::iterator p = ls.begin(); p != ls.end(); ++p) {
    spg_t pgid;
    if (!p->is_pg(&pgid))
      continue;

    // list temp objects
    dout(20) << " clearing temps in " << *p << " pgid " << pgid << dendl;

    vector<ghobject_t> temps;
    ghobject_t next;
    while (1) {
      vector<ghobject_t> objects;
      auto ch = store->open_collection(*p);
      ceph_assert(ch);
      store->collection_list(ch, next, ghobject_t::get_max(),
			     store->get_ideal_list_max(),
			     &objects, &next);
      if (objects.empty())
	break;
      vector<ghobject_t>::iterator q;
      for (q = objects.begin(); q != objects.end(); ++q) {
	// Hammer set pool for temps to -1, so check for clean-up
	if (q->hobj.is_temp() || (q->hobj.pool == -1)) {
	  temps.push_back(*q);
	} else {
	  break;
	}
      }
      // If we saw a non-temp object and hit the break above we can
      // break out of the while loop too.
      if (q != objects.end())
	break;
    }
    if (!temps.empty()) {
      ObjectStore::Transaction t;
      int removed = 0;
      for (vector<ghobject_t>::iterator q = temps.begin(); q != temps.end(); ++q) {
	dout(20) << "  removing " << *p << " object " << *q << dendl;
	t.remove(*p, *q);
        if (++removed > cct->_conf->osd_target_transaction_size) {
          store->queue_transaction(service.meta_ch, std::move(t));
          t = ObjectStore::Transaction();
          removed = 0;
        }
      }
      if (removed) {
        store->queue_transaction(service.meta_ch, std::move(t));
      }
    }
  }
}

void OSD::recursive_remove_collection(CephContext* cct,
				      ObjectStore *store, spg_t pgid,
				      coll_t tmp)
{
  OSDriver driver(
    store,
    coll_t(),
    make_snapmapper_oid());

  ObjectStore::CollectionHandle ch = store->open_collection(tmp);
  ObjectStore::Transaction t;
  SnapMapper mapper(cct, &driver, 0, 0, 0, pgid.shard);

  ghobject_t next;
  int max = cct->_conf->osd_target_transaction_size;
  vector<ghobject_t> objects;
  objects.reserve(max);
  while (true) {
    objects.clear();
    store->collection_list(ch, next, ghobject_t::get_max(),
      max, &objects, &next);
    generic_dout(10) << __func__ << " " << objects << dendl;
    if (objects.empty())
      break;
    for (auto& p: objects) {
      OSDriver::OSTransaction _t(driver.get_transaction(&t));
      int r = mapper.remove_oid(p.hobj, &_t);
      if (r != 0 && r != -ENOENT)
        ceph_abort();
      t.remove(tmp, p);
    }
    int r = store->queue_transaction(ch, std::move(t));
    ceph_assert(r == 0);
    t = ObjectStore::Transaction();
  }
  t.remove_collection(tmp);
  int r = store->queue_transaction(ch, std::move(t));
  ceph_assert(r == 0);

  C_SaferCond waiter;
  if (!ch->flush_commit(&waiter)) {
    waiter.wait();
  }
}


// ======================================================
// PG's

PG* OSD::_make_pg(
  OSDMapRef createmap,
  spg_t pgid)
{
  dout(10) << __func__ << " " << pgid << dendl;
  pg_pool_t pi;
  map<string,string> ec_profile;
  string name;
  if (createmap->have_pg_pool(pgid.pool())) {
    pi = *createmap->get_pg_pool(pgid.pool());
    name = createmap->get_pool_name(pgid.pool());
    if (pi.is_erasure()) {
      ec_profile = createmap->get_erasure_code_profile(pi.erasure_code_profile);
    }
  } else {
    // pool was deleted; grab final pg_pool_t off disk.
    ghobject_t oid = make_final_pool_info_oid(pgid.pool());
    bufferlist bl;
    int r = store->read(service.meta_ch, oid, 0, 0, bl);
    if (r < 0) {
      derr << __func__ << " missing pool " << pgid.pool() << " tombstone"
	   << dendl;
      return nullptr;
    }
    ceph_assert(r >= 0);
    auto p = bl.cbegin();
    decode(pi, p);
    decode(name, p);
    if (p.end()) { // dev release v13.0.2 did not include ec_profile
      derr << __func__ << " missing ec_profile from pool " << pgid.pool()
	   << " tombstone" << dendl;
      return nullptr;
    }
    decode(ec_profile, p);
  }
  PGPool pool(cct, createmap, pgid.pool(), pi, name);
  PG *pg;
  if (pi.type == pg_pool_t::TYPE_REPLICATED ||
      pi.type == pg_pool_t::TYPE_ERASURE)
    pg = new PrimaryLogPG(&service, createmap, pool, ec_profile, pgid);
  else
    ceph_abort();
  return pg;
}

void OSD::_get_pgs(vector<PGRef> *v, bool clear_too)
{
  v->clear();
  v->reserve(get_num_pgs());
  for (auto& s : shards) {
    std::lock_guard l(s->shard_lock);
    for (auto& j : s->pg_slots) {
      if (j.second->pg &&
	  !j.second->pg->is_deleted()) {
	v->push_back(j.second->pg);
	if (clear_too) {
	  s->_detach_pg(j.second.get());
	}
      }
    }
  }
}

void OSD::_get_pgids(vector<spg_t> *v)
{
  v->clear();
  v->reserve(get_num_pgs());
  for (auto& s : shards) {
    std::lock_guard l(s->shard_lock);
    for (auto& j : s->pg_slots) {
      if (j.second->pg &&
	  !j.second->pg->is_deleted()) {
	v->push_back(j.first);
      }
    }
  }
}

void OSD::register_pg(PGRef pg)
{
  spg_t pgid = pg->get_pgid();
  uint32_t shard_index = pgid.hash_to_shard(num_shards);
  auto sdata = shards[shard_index];
  std::lock_guard l(sdata->shard_lock);
  auto r = sdata->pg_slots.emplace(pgid, make_unique<OSDShardPGSlot>());
  ceph_assert(r.second);
  auto *slot = r.first->second.get();
  dout(20) << __func__ << " " << pgid << " " << pg << dendl;
  sdata->_attach_pg(slot, pg.get());
}

bool OSD::try_finish_pg_delete(PG *pg, unsigned old_pg_num)
{
  auto sdata = pg->osd_shard;
  ceph_assert(sdata);
  {
    std::lock_guard l(sdata->shard_lock);
    auto p = sdata->pg_slots.find(pg->pg_id);
    if (p == sdata->pg_slots.end() ||
	!p->second->pg) {
      dout(20) << __func__ << " " << pg->pg_id << " not found" << dendl;
      return false;
    }
    if (p->second->waiting_for_merge_epoch) {
      dout(20) << __func__ << " " << pg->pg_id << " waiting for merge" << dendl;
      return false;
    }
    dout(20) << __func__ << " " << pg->pg_id << " " << pg << dendl;
    sdata->_detach_pg(p->second.get());
  }

  for (auto shard : shards) {
    shard->unprime_split_children(pg->pg_id, old_pg_num);
  }

  // update pg count now since we might not get an osdmap any time soon.
  if (pg->is_primary())
    service.logger->dec(l_osd_pg_primary);
  else if (pg->is_replica())
    service.logger->dec(l_osd_pg_replica);
  else
    service.logger->dec(l_osd_pg_stray);

  return true;
}

PGRef OSD::_lookup_pg(spg_t pgid)
{
  uint32_t shard_index = pgid.hash_to_shard(num_shards);
  auto sdata = shards[shard_index];
  std::lock_guard l(sdata->shard_lock);
  auto p = sdata->pg_slots.find(pgid);
  if (p == sdata->pg_slots.end()) {
    return nullptr;
  }
  return p->second->pg;
}

PGRef OSD::_lookup_lock_pg(spg_t pgid)
{
  PGRef pg = _lookup_pg(pgid);
  if (!pg) {
    return nullptr;
  }
  pg->lock();
  if (!pg->is_deleted()) {
    return pg;
  }
  pg->unlock();
  return nullptr;
}

PGRef OSD::lookup_lock_pg(spg_t pgid)
{
  return _lookup_lock_pg(pgid);
}

void OSD::load_pgs()
{
  ceph_assert(osd_lock.is_locked());
  dout(0) << "load_pgs" << dendl;

  {
    auto pghist = make_pg_num_history_oid();
    bufferlist bl;
    int r = store->read(service.meta_ch, pghist, 0, 0, bl, 0);
    if (r >= 0 && bl.length() > 0) {
      auto p = bl.cbegin();
      decode(pg_num_history, p);
    }
    dout(20) << __func__ << " pg_num_history " << pg_num_history << dendl;
  }

  vector<coll_t> ls;
  int r = store->list_collections(ls);
  if (r < 0) {
    derr << "failed to list pgs: " << cpp_strerror(-r) << dendl;
  }

  int num = 0;
  for (vector<coll_t>::iterator it = ls.begin();
       it != ls.end();
       ++it) {
    spg_t pgid;
    if (it->is_temp(&pgid) ||
       (it->is_pg(&pgid) && PG::_has_removal_flag(store, pgid))) {
      dout(10) << "load_pgs " << *it
	       << " removing, legacy or flagged for removal pg" << dendl;
      recursive_remove_collection(cct, store, pgid, *it);
      continue;
    }

    if (!it->is_pg(&pgid)) {
      dout(10) << "load_pgs ignoring unrecognized " << *it << dendl;
      continue;
    }

    dout(10) << "pgid " << pgid << " coll " << coll_t(pgid) << dendl;
    epoch_t map_epoch = 0;
    int r = PG::peek_map_epoch(store, pgid, &map_epoch);
    if (r < 0) {
      derr << __func__ << " unable to peek at " << pgid << " metadata, skipping"
	   << dendl;
      continue;
    }

    PGRef pg;
    if (map_epoch > 0) {
      OSDMapRef pgosdmap = service.try_get_map(map_epoch);
      if (!pgosdmap) {
	if (!osdmap->have_pg_pool(pgid.pool())) {
	  derr << __func__ << ": could not find map for epoch " << map_epoch
	       << " on pg " << pgid << ", but the pool is not present in the "
	       << "current map, so this is probably a result of bug 10617.  "
	       << "Skipping the pg for now, you can use ceph-objectstore-tool "
	       << "to clean it up later." << dendl;
	  continue;
	} else {
	  derr << __func__ << ": have pgid " << pgid << " at epoch "
	       << map_epoch << ", but missing map.  Crashing."
	       << dendl;
	  ceph_abort_msg("Missing map in load_pgs");
	}
      }
      pg = _make_pg(pgosdmap, pgid);
    } else {
      pg = _make_pg(osdmap, pgid);
    }
    if (!pg) {
      recursive_remove_collection(cct, store, pgid, *it);
      continue;
    }

    // there can be no waiters here, so we don't call _wake_pg_slot

    pg->lock();
    pg->ch = store->open_collection(pg->coll);

    // read pg state, log
    pg->read_state(store);

    if (pg->dne())  {
      dout(10) << "load_pgs " << *it << " deleting dne" << dendl;
      pg->ch = nullptr;
      pg->unlock();
      recursive_remove_collection(cct, store, pgid, *it);
      continue;
    }
    {
      uint32_t shard_index = pgid.hash_to_shard(shards.size());
      assert(NULL != shards[shard_index]);
      store->set_collection_commit_queue(pg->coll, &(shards[shard_index]->context_queue));
    }

    pg->reg_next_scrub();

    dout(10) << __func__ << " loaded " << *pg << dendl;
    pg->unlock();

    register_pg(pg);
    ++num;
  }
  dout(0) << __func__ << " opened " << num << " pgs" << dendl;
}


PGRef OSD::handle_pg_create_info(const OSDMapRef& osdmap,
				 const PGCreateInfo *info)
{
  spg_t pgid = info->pgid;

  if (maybe_wait_for_max_pg(osdmap, pgid, info->by_mon)) {
    dout(10) << __func__ << " hit max pg, dropping" << dendl;
    return nullptr;
  }

  PeeringCtx rctx = create_context();

  OSDMapRef startmap = get_map(info->epoch);

  if (info->by_mon) {
    int64_t pool_id = pgid.pgid.pool();
    const pg_pool_t *pool = osdmap->get_pg_pool(pool_id);
    if (!pool) {
      dout(10) << __func__ << " ignoring " << pgid << ", pool dne" << dendl;
      return nullptr;
    }
    if (osdmap->require_osd_release >= ceph_release_t::nautilus &&
	!pool->has_flag(pg_pool_t::FLAG_CREATING)) {
      // this ensures we do not process old creating messages after the
      // pool's initial pgs have been created (and pg are subsequently
      // allowed to split or merge).
      dout(20) << __func__ << "  dropping " << pgid
	       << "create, pool does not have CREATING flag set" << dendl;
      return nullptr;
    }
  }

  int up_primary, acting_primary;
  vector<int> up, acting;
  startmap->pg_to_up_acting_osds(
    pgid.pgid, &up, &up_primary, &acting, &acting_primary);

  const pg_pool_t* pp = startmap->get_pg_pool(pgid.pool());
  if (pp->has_flag(pg_pool_t::FLAG_EC_OVERWRITES) &&
      store->get_type() != "bluestore") {
    clog->warn() << "pg " << pgid
		 << " is at risk of silent data corruption: "
		 << "the pool allows ec overwrites but is not stored in "
		 << "bluestore, so deep scrubbing will not detect bitrot";
  }
  create_pg_collection(
    rctx.transaction, pgid, pgid.get_split_bits(pp->get_pg_num()));
  init_pg_ondisk(rctx.transaction, pgid, pp);

  int role = startmap->calc_pg_role(whoami, acting, acting.size());
  if (!pp->is_replicated() && role != pgid.shard) {
    role = -1;
  }

  PGRef pg = _make_pg(startmap, pgid);
  pg->ch = store->create_new_collection(pg->coll);

  {
    uint32_t shard_index = pgid.hash_to_shard(shards.size());
    assert(NULL != shards[shard_index]);
    store->set_collection_commit_queue(pg->coll, &(shards[shard_index]->context_queue));
  }

  pg->lock(true);

  // we are holding the shard lock
  ceph_assert(!pg->is_deleted());

  pg->init(
    role,
    up,
    up_primary,
    acting,
    acting_primary,
    info->history,
    info->past_intervals,
    false,
    rctx.transaction);

  if (pg->is_primary()) {
    Mutex::Locker locker(m_perf_queries_lock);
    pg->set_dynamic_perf_stats_queries(m_perf_queries);
  }

  pg->handle_initialize(rctx);
  pg->handle_activate_map(rctx);

  dispatch_context(rctx, pg.get(), osdmap, nullptr);

  dout(10) << __func__ << " new pg " << *pg << dendl;
  return pg;
}

bool OSD::maybe_wait_for_max_pg(const OSDMapRef& osdmap,
				spg_t pgid,
				bool is_mon_create)
{
  const auto max_pgs_per_osd =
    (cct->_conf.get_val<uint64_t>("mon_max_pg_per_osd") *
     cct->_conf.get_val<double>("osd_max_pg_per_osd_hard_ratio"));

  if (num_pgs < max_pgs_per_osd) {
    return false;
  }

  std::lock_guard l(pending_creates_lock);
  if (is_mon_create) {
    pending_creates_from_mon++;
  } else {
    bool is_primary = osdmap->get_pg_acting_rank(pgid.pgid, whoami) == 0;
    pending_creates_from_osd.emplace(pgid.pgid, is_primary);
  }
  dout(1) << __func__ << " withhold creation of pg " << pgid
	  << ": " << num_pgs << " >= "<< max_pgs_per_osd << dendl;
  return true;
}

// to re-trigger a peering, we have to twiddle the pg mapping a little bit,
// see PG::should_restart_peering(). OSDMap::pg_to_up_acting_osds() will turn
// to up set if pg_temp is empty. so an empty pg_temp won't work.
static vector<int32_t> twiddle(const vector<int>& acting) {
  if (acting.size() > 1) {
    return {acting[0]};
  } else {
    vector<int32_t> twiddled(acting.begin(), acting.end());
    twiddled.push_back(-1);
    return twiddled;
  }
}

void OSD::resume_creating_pg()
{
  bool do_sub_pg_creates = false;
  bool have_pending_creates = false;
  {
    const auto max_pgs_per_osd =
      (cct->_conf.get_val<uint64_t>("mon_max_pg_per_osd") *
       cct->_conf.get_val<double>("osd_max_pg_per_osd_hard_ratio"));
    if (max_pgs_per_osd <= num_pgs) {
      // this could happen if admin decreases this setting before a PG is removed
      return;
    }
    unsigned spare_pgs = max_pgs_per_osd - num_pgs;
    std::lock_guard l(pending_creates_lock);
    if (pending_creates_from_mon > 0) {
      dout(20) << __func__ << " pending_creates_from_mon "
	       << pending_creates_from_mon << dendl;
      do_sub_pg_creates = true;
      if (pending_creates_from_mon >= spare_pgs) {
	spare_pgs = pending_creates_from_mon = 0;
      } else {
	spare_pgs -= pending_creates_from_mon;
	pending_creates_from_mon = 0;
      }
    }
    auto pg = pending_creates_from_osd.cbegin();
    while (spare_pgs > 0 && pg != pending_creates_from_osd.cend()) {
      dout(20) << __func__ << " pg " << pg->first << dendl;
      vector<int> acting;
      osdmap->pg_to_up_acting_osds(pg->first, nullptr, nullptr, &acting, nullptr);
      service.queue_want_pg_temp(pg->first, twiddle(acting), true);
      pg = pending_creates_from_osd.erase(pg);
      do_sub_pg_creates = true;
      spare_pgs--;
    }
    have_pending_creates = (pending_creates_from_mon > 0 ||
			    !pending_creates_from_osd.empty());
  }

  bool do_renew_subs = false;
  if (do_sub_pg_creates) {
    if (monc->sub_want("osd_pg_creates", last_pg_create_epoch, 0)) {
      dout(4) << __func__ << ": resolicit pg creates from mon since "
	      << last_pg_create_epoch << dendl;
      do_renew_subs = true;
    }
  }
  version_t start = osdmap->get_epoch() + 1;
  if (have_pending_creates) {
    // don't miss any new osdmap deleting PGs
    if (monc->sub_want("osdmap", start, 0)) {
      dout(4) << __func__ << ": resolicit osdmap from mon since "
	      << start << dendl;
      do_renew_subs = true;
    }
  } else if (do_sub_pg_creates) {
    // no need to subscribe the osdmap continuously anymore
    // once the pgtemp and/or mon_subscribe(pg_creates) is sent
    if (monc->sub_want_increment("osdmap", start, CEPH_SUBSCRIBE_ONETIME)) {
      dout(4) << __func__ << ": re-subscribe osdmap(onetime) since "
	      << start << dendl;
      do_renew_subs = true;
    }
  }

  if (do_renew_subs) {
    monc->renew_subs();
  }

  service.send_pg_temp();
}

void OSD::build_initial_pg_history(
  spg_t pgid,
  epoch_t created,
  utime_t created_stamp,
  pg_history_t *h,
  PastIntervals *pi)
{
  dout(10) << __func__ << " " << pgid << " created " << created << dendl;
  *h = pg_history_t(created, created_stamp);

  OSDMapRef lastmap = service.get_map(created);
  int up_primary, acting_primary;
  vector<int> up, acting;
  lastmap->pg_to_up_acting_osds(
    pgid.pgid, &up, &up_primary, &acting, &acting_primary);

  ostringstream debug;
  for (epoch_t e = created + 1; e <= osdmap->get_epoch(); ++e) {
    OSDMapRef osdmap = service.get_map(e);
    int new_up_primary, new_acting_primary;
    vector<int> new_up, new_acting;
    osdmap->pg_to_up_acting_osds(
      pgid.pgid, &new_up, &new_up_primary, &new_acting, &new_acting_primary);

    // this is a bit imprecise, but sufficient?
    struct min_size_predicate_t : public IsPGRecoverablePredicate {
      const pg_pool_t *pi;
      bool operator()(const set<pg_shard_t> &have) const {
	return have.size() >= pi->min_size;
      }
      explicit min_size_predicate_t(const pg_pool_t *i) : pi(i) {}
    } min_size_predicate(osdmap->get_pg_pool(pgid.pgid.pool()));

    bool new_interval = PastIntervals::check_new_interval(
      acting_primary,
      new_acting_primary,
      acting, new_acting,
      up_primary,
      new_up_primary,
      up, new_up,
      h->same_interval_since,
      h->last_epoch_clean,
      osdmap.get(),
      lastmap.get(),
      pgid.pgid,
      min_size_predicate,
      pi,
      &debug);
    if (new_interval) {
      h->same_interval_since = e;
      if (up != new_up) {
        h->same_up_since = e;
      }
      if (acting_primary != new_acting_primary) {
        h->same_primary_since = e;
      }
      if (pgid.pgid.is_split(lastmap->get_pg_num(pgid.pgid.pool()),
                             osdmap->get_pg_num(pgid.pgid.pool()),
                             nullptr)) {
        h->last_epoch_split = e;
      }
      up = new_up;
      acting = new_acting;
      up_primary = new_up_primary;
      acting_primary = new_acting_primary;
    }
    lastmap = osdmap;
  }
  dout(20) << __func__ << " " << debug.str() << dendl;
  dout(10) << __func__ << " " << *h << " " << *pi
	   << " [" << (pi->empty() ? pair<epoch_t,epoch_t>(0,0) :
		       pi->get_bounds()) << ")"
	   << dendl;
}

void OSD::_add_heartbeat_peer(int p)
{
  if (p == whoami)
    return;
  HeartbeatInfo *hi;

  map<int,HeartbeatInfo>::iterator i = heartbeat_peers.find(p);
  if (i == heartbeat_peers.end()) {
    pair<ConnectionRef,ConnectionRef> cons = service.get_con_osd_hb(p, osdmap->get_epoch());
    if (!cons.first)
      return;
    hi = &heartbeat_peers[p];
    hi->peer = p;
    RefCountedPtr s{new HeartbeatSession{p}, false};
    hi->con_back = cons.first.get();
    hi->con_back->set_priv(s);
    if (cons.second) {
      hi->con_front = cons.second.get();
      hi->con_front->set_priv(s);
      dout(10) << "_add_heartbeat_peer: new peer osd." << p
	       << " " << hi->con_back->get_peer_addr()
	       << " " << hi->con_front->get_peer_addr()
	       << dendl;
    } else {
      hi->con_front.reset(NULL);
      dout(10) << "_add_heartbeat_peer: new peer osd." << p
	       << " " << hi->con_back->get_peer_addr()
	       << dendl;
    }
  } else {
    hi = &i->second;
  }
  hi->epoch = osdmap->get_epoch();
}

void OSD::_remove_heartbeat_peer(int n)
{
  map<int,HeartbeatInfo>::iterator q = heartbeat_peers.find(n);
  ceph_assert(q != heartbeat_peers.end());
  dout(20) << " removing heartbeat peer osd." << n
	   << " " << q->second.con_back->get_peer_addr()
	   << " " << (q->second.con_front ? q->second.con_front->get_peer_addr() : entity_addr_t())
	   << dendl;
  q->second.con_back->mark_down();
  if (q->second.con_front) {
    q->second.con_front->mark_down();
  }
  heartbeat_peers.erase(q);
}

void OSD::need_heartbeat_peer_update()
{
  if (is_stopping())
    return;
  dout(20) << "need_heartbeat_peer_update" << dendl;
  heartbeat_set_peers_need_update();
}

void OSD::maybe_update_heartbeat_peers()
{
  ceph_assert(osd_lock.is_locked());

  if (is_waiting_for_healthy() || is_active()) {
    utime_t now = ceph_clock_now();
    if (last_heartbeat_resample == utime_t()) {
      last_heartbeat_resample = now;
      heartbeat_set_peers_need_update();
    } else if (!heartbeat_peers_need_update()) {
      utime_t dur = now - last_heartbeat_resample;
      if (dur > cct->_conf->osd_heartbeat_grace) {
	dout(10) << "maybe_update_heartbeat_peers forcing update after " << dur << " seconds" << dendl;
	heartbeat_set_peers_need_update();
	last_heartbeat_resample = now;
	// automatically clean up any stale heartbeat peers
	// if we are unhealthy, then clean all
	reset_heartbeat_peers(is_waiting_for_healthy());
      }
    }
  }

  if (!heartbeat_peers_need_update())
    return;
  heartbeat_clear_peers_need_update();

  std::lock_guard l(heartbeat_lock);

  dout(10) << "maybe_update_heartbeat_peers updating" << dendl;


  // build heartbeat from set
  if (is_active()) {
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto& pg : pgs) {
      pg->with_heartbeat_peers([&](int peer) {
	  if (osdmap->is_up(peer)) {
	    _add_heartbeat_peer(peer);
	  }
	});
    }
  }

  // include next and previous up osds to ensure we have a fully-connected set
  set<int> want, extras;
  const int next = osdmap->get_next_up_osd_after(whoami);
  if (next >= 0)
    want.insert(next);
  int prev = osdmap->get_previous_up_osd_before(whoami);
  if (prev >= 0 && prev != next)
    want.insert(prev);

  // make sure we have at least **min_down** osds coming from different
  // subtree level (e.g., hosts) for fast failure detection.
  auto min_down = cct->_conf.get_val<uint64_t>("mon_osd_min_down_reporters");
  auto subtree = cct->_conf.get_val<string>("mon_osd_reporter_subtree_level");
  osdmap->get_random_up_osds_by_subtree(
    whoami, subtree, min_down, want, &want);

  for (set<int>::iterator p = want.begin(); p != want.end(); ++p) {
    dout(10) << " adding neighbor peer osd." << *p << dendl;
    extras.insert(*p);
    _add_heartbeat_peer(*p);
  }

  // remove down peers; enumerate extras
  map<int,HeartbeatInfo>::iterator p = heartbeat_peers.begin();
  while (p != heartbeat_peers.end()) {
    if (!osdmap->is_up(p->first)) {
      int o = p->first;
      ++p;
      _remove_heartbeat_peer(o);
      continue;
    }
    if (p->second.epoch < osdmap->get_epoch()) {
      extras.insert(p->first);
    }
    ++p;
  }

  // too few?
  for (int n = next; n >= 0; ) {
    if ((int)heartbeat_peers.size() >= cct->_conf->osd_heartbeat_min_peers)
      break;
    if (!extras.count(n) && !want.count(n) && n != whoami) {
      dout(10) << " adding random peer osd." << n << dendl;
      extras.insert(n);
      _add_heartbeat_peer(n);
    }
    n = osdmap->get_next_up_osd_after(n);
    if (n == next)
      break;  // came full circle; stop
  }

  // too many?
  for (set<int>::iterator p = extras.begin();
       (int)heartbeat_peers.size() > cct->_conf->osd_heartbeat_min_peers && p != extras.end();
       ++p) {
    if (want.count(*p))
      continue;
    _remove_heartbeat_peer(*p);
  }

  dout(10) << "maybe_update_heartbeat_peers " << heartbeat_peers.size() << " peers, extras " << extras << dendl;
}

void OSD::reset_heartbeat_peers(bool all)
{
  ceph_assert(osd_lock.is_locked());
  dout(10) << "reset_heartbeat_peers" << dendl;
  utime_t stale = ceph_clock_now();
  stale -= cct->_conf.get_val<int64_t>("osd_heartbeat_stale");
  std::lock_guard l(heartbeat_lock);
  for (auto it = heartbeat_peers.begin(); it != heartbeat_peers.end();) {
    HeartbeatInfo& hi = it->second;
    if (all || hi.is_stale(stale)) {
      hi.con_back->mark_down();
      if (hi.con_front) {
        hi.con_front->mark_down();
      }
      // stop sending failure_report to mon too
      failure_queue.erase(it->first);
      heartbeat_peers.erase(it++);
    } else {
      it++;
    }
  }
}

void OSD::handle_osd_ping(MOSDPing *m)
{
  if (superblock.cluster_fsid != m->fsid) {
    dout(20) << "handle_osd_ping from " << m->get_source_inst()
	     << " bad fsid " << m->fsid << " != " << superblock.cluster_fsid << dendl;
    m->put();
    return;
  }

  int from = m->get_source().num();

  heartbeat_lock.Lock();
  if (is_stopping()) {
    heartbeat_lock.Unlock();
    m->put();
    return;
  }

  utime_t now = ceph_clock_now();
  ConnectionRef con(m->get_connection());
  OSDMapRef curmap = service.get_osdmap();
  if (!curmap) {
    heartbeat_lock.Unlock();
    m->put();
    return;
  }

  switch (m->op) {

  case MOSDPing::PING:
    {
      if (cct->_conf->osd_debug_drop_ping_probability > 0) {
	auto heartbeat_drop = debug_heartbeat_drops_remaining.find(from);
	if (heartbeat_drop != debug_heartbeat_drops_remaining.end()) {
	  if (heartbeat_drop->second == 0) {
	    debug_heartbeat_drops_remaining.erase(heartbeat_drop);
	  } else {
	    --heartbeat_drop->second;
	    dout(5) << "Dropping heartbeat from " << from
		    << ", " << heartbeat_drop->second
		    << " remaining to drop" << dendl;
	    break;
	  }
	} else if (cct->_conf->osd_debug_drop_ping_probability >
	           ((((double)(rand()%100))/100.0))) {
	  heartbeat_drop =
	    debug_heartbeat_drops_remaining.insert(std::make_pair(from,
	                     cct->_conf->osd_debug_drop_ping_duration)).first;
	  dout(5) << "Dropping heartbeat from " << from
		  << ", " << heartbeat_drop->second
		  << " remaining to drop" << dendl;
	  break;
	}
      }

      if (!cct->get_heartbeat_map()->is_healthy()) {
	dout(10) << "internal heartbeat not healthy, dropping ping request"
		 << dendl;
	break;
      }

      Message *r = new MOSDPing(monc->get_fsid(),
				curmap->get_epoch(),
				MOSDPing::PING_REPLY,
				m->stamp,
				service.get_up_epoch(),
				get_min_pg_epoch(),
				cct->_conf->osd_heartbeat_min_size);
      con->send_message(r);

      if (curmap->is_up(from)) {
	if (is_active()) {
	  ConnectionRef cluster_con = service.get_con_osd_cluster(
	    from, curmap->get_epoch());
	  if (cluster_con) {
	    service.maybe_share_map(cluster_con.get(), curmap, m->map_epoch);
	  }
	}
      } else if (!curmap->exists(from) ||
		 curmap->get_down_at(from) > m->map_epoch) {
	// tell them they have died
	Message *r = new MOSDPing(monc->get_fsid(),
				  curmap->get_epoch(),
				  MOSDPing::YOU_DIED,
				  m->stamp,
				  service.get_up_epoch(),
				  get_min_pg_epoch(),
				  cct->_conf->osd_heartbeat_min_size);
	con->send_message(r);
      }
    }
    break;

  case MOSDPing::PING_REPLY:
    {
      map<int,HeartbeatInfo>::iterator i = heartbeat_peers.find(from);
      if (i != heartbeat_peers.end()) {
        auto acked = i->second.ping_history.find(m->stamp);
        if (acked != i->second.ping_history.end()) {
          int &unacknowledged = acked->second.second;
          if (con == i->second.con_back) {
            dout(25) << "handle_osd_ping got reply from osd." << from
                     << " first_tx " << i->second.first_tx
                     << " last_tx " << i->second.last_tx
                     << " last_rx_back " << i->second.last_rx_back
		     << " -> " << now
                     << " last_rx_front " << i->second.last_rx_front
                     << dendl;
            i->second.last_rx_back = now;
            ceph_assert(unacknowledged > 0);
            --unacknowledged;
            // if there is no front con, set both stamps.
            if (i->second.con_front == NULL) {
              i->second.last_rx_front = now;
              ceph_assert(unacknowledged > 0);
              --unacknowledged;
            }
          } else if (con == i->second.con_front) {
            dout(25) << "handle_osd_ping got reply from osd." << from
                     << " first_tx " << i->second.first_tx
                     << " last_tx " << i->second.last_tx
                     << " last_rx_back " << i->second.last_rx_back
                     << " last_rx_front " << i->second.last_rx_front
		     << " -> " << now
                     << dendl;
            i->second.last_rx_front = now;
            ceph_assert(unacknowledged > 0);
            --unacknowledged;
          }

          if (unacknowledged == 0) {
            // succeeded in getting all replies
            dout(25) << "handle_osd_ping got all replies from osd." << from
                     << " , erase pending ping(sent at " << m->stamp << ")"
                     << " and older pending ping(s)"
                     << dendl;
            i->second.ping_history.erase(i->second.ping_history.begin(), ++acked);
          }

          if (i->second.is_healthy(now)) {
            // Cancel false reports
            auto failure_queue_entry = failure_queue.find(from);
            if (failure_queue_entry != failure_queue.end()) {
              dout(10) << "handle_osd_ping canceling queued "
                       << "failure report for osd." << from << dendl;
              failure_queue.erase(failure_queue_entry);
            }

            auto failure_pending_entry = failure_pending.find(from);
            if (failure_pending_entry != failure_pending.end()) {
              dout(10) << "handle_osd_ping canceling in-flight "
                       << "failure report for osd." << from << dendl;
              send_still_alive(curmap->get_epoch(),
                               from,
                               failure_pending_entry->second.second);
              failure_pending.erase(failure_pending_entry);
            }
          }
        } else {
          // old replies, deprecated by newly sent pings.
          dout(10) << "handle_osd_ping no pending ping(sent at " << m->stamp
                   << ") is found, treat as covered by newly sent pings "
                   << "and ignore"
                   << dendl;
        }
      }

      if (m->map_epoch &&
	  curmap->is_up(from)) {
	if (is_active()) {
	  ConnectionRef cluster_con = service.get_con_osd_cluster(
	    from, curmap->get_epoch());
	  if (cluster_con) {
	    service.maybe_share_map(cluster_con.get(), curmap, m->map_epoch);
	  }
	}
      }
    }
    break;

  case MOSDPing::YOU_DIED:
    dout(10) << "handle_osd_ping " << m->get_source_inst()
	     << " says i am down in " << m->map_epoch << dendl;
    osdmap_subscribe(curmap->get_epoch()+1, false);
    break;
  }

  heartbeat_lock.Unlock();
  m->put();
}

void OSD::heartbeat_entry()
{
  std::lock_guard l(heartbeat_lock);
  if (is_stopping())
    return;
  while (!heartbeat_stop) {
    heartbeat();

    double wait = .5 + ((float)(rand() % 10)/10.0) * (float)cct->_conf->osd_heartbeat_interval;
    utime_t w;
    w.set_from_double(wait);
    dout(30) << "heartbeat_entry sleeping for " << wait << dendl;
    heartbeat_cond.WaitInterval(heartbeat_lock, w);
    if (is_stopping())
      return;
    dout(30) << "heartbeat_entry woke up" << dendl;
  }
}

void OSD::heartbeat_check()
{
  ceph_assert(heartbeat_lock.is_locked());
  utime_t now = ceph_clock_now();

  // check for incoming heartbeats (move me elsewhere?)
  for (map<int,HeartbeatInfo>::iterator p = heartbeat_peers.begin();
       p != heartbeat_peers.end();
       ++p) {

    if (p->second.first_tx == utime_t()) {
      dout(25) << "heartbeat_check we haven't sent ping to osd." << p->first
               << " yet, skipping" << dendl;
      continue;
    }

    dout(25) << "heartbeat_check osd." << p->first
	     << " first_tx " << p->second.first_tx
	     << " last_tx " << p->second.last_tx
	     << " last_rx_back " << p->second.last_rx_back
	     << " last_rx_front " << p->second.last_rx_front
	     << dendl;
    if (p->second.is_unhealthy(now)) {
      utime_t oldest_deadline = p->second.ping_history.begin()->second.first;
      if (p->second.last_rx_back == utime_t() ||
	  p->second.last_rx_front == utime_t()) {
        derr << "heartbeat_check: no reply from "
             << p->second.con_front->get_peer_addr().get_sockaddr()
             << " osd." << p->first
             << " ever on either front or back, first ping sent "
             << p->second.first_tx
             << " (oldest deadline " << oldest_deadline << ")"
             << dendl;
	// fail
	failure_queue[p->first] = p->second.first_tx;
      } else {
	derr << "heartbeat_check: no reply from "
             << p->second.con_front->get_peer_addr().get_sockaddr()
	     << " osd." << p->first << " since back " << p->second.last_rx_back
	     << " front " << p->second.last_rx_front
	     << " (oldest deadline " << oldest_deadline << ")"
             << dendl;
	// fail
	failure_queue[p->first] = std::min(p->second.last_rx_back, p->second.last_rx_front);
      }
    }
  }
}

void OSD::heartbeat()
{
  ceph_assert(heartbeat_lock.is_locked_by_me());
  dout(30) << "heartbeat" << dendl;

  // get CPU load avg
  double loadavgs[1];
  int hb_interval = cct->_conf->osd_heartbeat_interval;
  int n_samples = 86400;
  if (hb_interval > 1) {
    n_samples /= hb_interval;
    if (n_samples < 1)
      n_samples = 1;
  }

  if (getloadavg(loadavgs, 1) == 1) {
    logger->set(l_osd_loadavg, 100 * loadavgs[0]);
    daily_loadavg = (daily_loadavg * (n_samples - 1) + loadavgs[0]) / n_samples;
    dout(30) << "heartbeat: daily_loadavg " << daily_loadavg << dendl;
  }

  dout(30) << "heartbeat checking stats" << dendl;

  // refresh peer list and osd stats
  vector<int> hb_peers;
  for (map<int,HeartbeatInfo>::iterator p = heartbeat_peers.begin();
       p != heartbeat_peers.end();
       ++p)
    hb_peers.push_back(p->first);

  auto new_stat = service.set_osd_stat(hb_peers, get_num_pgs());
  dout(5) << __func__ << " " << new_stat << dendl;
  ceph_assert(new_stat.statfs.total);

  float pratio;
  float ratio = service.compute_adjusted_ratio(new_stat, &pratio);

  service.check_full_status(ratio, pratio);

  utime_t now = ceph_clock_now();
  utime_t deadline = now;
  deadline += cct->_conf->osd_heartbeat_grace;

  // send heartbeats
  auto min_pg_epoch = get_min_pg_epoch();
  for (map<int,HeartbeatInfo>::iterator i = heartbeat_peers.begin();
       i != heartbeat_peers.end();
       ++i) {
    int peer = i->first;
    i->second.last_tx = now;
    if (i->second.first_tx == utime_t())
      i->second.first_tx = now;
    i->second.ping_history[now] = make_pair(deadline,
      HeartbeatInfo::HEARTBEAT_MAX_CONN);
    dout(30) << "heartbeat sending ping to osd." << peer << dendl;
    i->second.con_back->send_message(
      new MOSDPing(monc->get_fsid(),
		   service.get_osdmap_epoch(),
		   MOSDPing::PING,
		   now,
		   service.get_up_epoch(),
		   min_pg_epoch,
		   cct->_conf->osd_heartbeat_min_size));

    if (i->second.con_front)
      i->second.con_front->send_message(
	new MOSDPing(monc->get_fsid(),
		     service.get_osdmap_epoch(),
		     MOSDPing::PING,
		     now,
		     service.get_up_epoch(),
		     min_pg_epoch,
		     cct->_conf->osd_heartbeat_min_size));
  }

  logger->set(l_osd_hb_to, heartbeat_peers.size());

  // hmm.. am i all alone?
  dout(30) << "heartbeat lonely?" << dendl;
  if (heartbeat_peers.empty()) {
    if (now - last_mon_heartbeat > cct->_conf->osd_mon_heartbeat_interval && is_active()) {
      last_mon_heartbeat = now;
      dout(10) << "i have no heartbeat peers; checking mon for new map" << dendl;
      osdmap_subscribe(osdmap->get_epoch() + 1, false);
    }
  }

  dout(30) << "heartbeat done" << dendl;
}

bool OSD::heartbeat_reset(Connection *con)
{
  std::lock_guard l(heartbeat_lock);
  auto s = con->get_priv();
  con->set_priv(nullptr);
  if (s) {
    if (is_stopping()) {
      return true;
    }
    auto heartbeat_session = static_cast<HeartbeatSession*>(s.get());
    auto p = heartbeat_peers.find(heartbeat_session->peer);
    if (p != heartbeat_peers.end() &&
	(p->second.con_back == con ||
	 p->second.con_front == con)) {
      dout(10) << "heartbeat_reset failed hb con " << con << " for osd." << p->second.peer
	       << ", reopening" << dendl;
      if (con != p->second.con_back) {
	p->second.con_back->mark_down();
      }
      p->second.con_back.reset(NULL);
      if (p->second.con_front && con != p->second.con_front) {
	p->second.con_front->mark_down();
      }
      p->second.con_front.reset(NULL);
      pair<ConnectionRef,ConnectionRef> newcon = service.get_con_osd_hb(p->second.peer, p->second.epoch);
      if (newcon.first) {
	p->second.con_back = newcon.first.get();
	p->second.con_back->set_priv(s);
	if (newcon.second) {
	  p->second.con_front = newcon.second.get();
	  p->second.con_front->set_priv(s);
	}
        p->second.ping_history.clear();
      } else {
	dout(10) << "heartbeat_reset failed hb con " << con << " for osd." << p->second.peer
		 << ", raced with osdmap update, closing out peer" << dendl;
	heartbeat_peers.erase(p);
      }
    } else {
      dout(10) << "heartbeat_reset closing (old) failed hb con " << con << dendl;
    }
  }
  return true;
}



// =========================================

void OSD::tick()
{
  ceph_assert(osd_lock.is_locked());
  dout(10) << "tick" << dendl;

  if (is_active() || is_waiting_for_healthy()) {
    maybe_update_heartbeat_peers();
  }

  if (is_waiting_for_healthy()) {
    start_boot();
  }

  if (is_waiting_for_healthy() || is_booting()) {
    std::lock_guard l(heartbeat_lock);
    utime_t now = ceph_clock_now();
    if (now - last_mon_heartbeat > cct->_conf->osd_mon_heartbeat_interval) {
      last_mon_heartbeat = now;
      dout(1) << __func__ << " checking mon for new map" << dendl;
      osdmap_subscribe(osdmap->get_epoch() + 1, false);
    }
  }

  do_waiters();

  // scrub purged_snaps every deep scrub interval
  {
    const utime_t last = superblock.last_purged_snaps_scrub;
    utime_t next = last;
    next += cct->_conf->osd_scrub_min_interval;
    std::mt19937 rng;
    // use a seed that is stable for each scrub interval, but varies
    // by OSD to avoid any herds.
    rng.seed(whoami + superblock.last_purged_snaps_scrub.sec());
    double r = (rng() % 1024) / 1024;
    next +=
      cct->_conf->osd_scrub_min_interval *
      cct->_conf->osd_scrub_interval_randomize_ratio * r;
    if (next < ceph_clock_now()) {
      dout(20) << __func__ << " last_purged_snaps_scrub " << last
	       << " next " << next << " ... now" << dendl;
      scrub_purged_snaps();
    } else {
      dout(20) << __func__ << " last_purged_snaps_scrub " << last
	       << " next " << next << dendl;
    }
  }

  tick_timer.add_event_after(get_tick_interval(), new C_Tick(this));
}

void OSD::tick_without_osd_lock()
{
  ceph_assert(tick_timer_lock.is_locked());
  dout(10) << "tick_without_osd_lock" << dendl;

  logger->set(l_osd_cached_crc, buffer::get_cached_crc());
  logger->set(l_osd_cached_crc_adjusted, buffer::get_cached_crc_adjusted());
  logger->set(l_osd_missed_crc, buffer::get_missed_crc());

  // refresh osd stats
  struct store_statfs_t stbuf;
  osd_alert_list_t alerts;
  int r = store->statfs(&stbuf, &alerts);
  ceph_assert(r == 0);
  service.set_statfs(stbuf, alerts);

  // osd_lock is not being held, which means the OSD state
  // might change when doing the monitor report
  if (is_active() || is_waiting_for_healthy()) {
    heartbeat_lock.Lock();
    heartbeat_check();
    heartbeat_lock.Unlock();

    map_lock.get_read();
    std::lock_guard l(mon_report_lock);

    // mon report?
    utime_t now = ceph_clock_now();
    if (service.need_fullness_update() ||
	now - last_mon_report > cct->_conf->osd_mon_report_interval) {
      last_mon_report = now;
      send_full_update();
      send_failures();
    }
    map_lock.put_read();

    epoch_t max_waiting_epoch = 0;
    for (auto s : shards) {
      max_waiting_epoch = std::max(max_waiting_epoch,
				   s->get_max_waiting_epoch());
    }
    if (max_waiting_epoch > get_osdmap()->get_epoch()) {
      dout(20) << __func__ << " max_waiting_epoch " << max_waiting_epoch
	       << ", requesting new map" << dendl;
      osdmap_subscribe(superblock.newest_map + 1, false);
    }
  }

  if (is_active()) {
    if (!scrub_random_backoff()) {
      sched_scrub();
    }
    service.promote_throttle_recalibrate();
    resume_creating_pg();
    bool need_send_beacon = false;
    const auto now = ceph::coarse_mono_clock::now();
    {
      // borrow lec lock to pretect last_sent_beacon from changing
      std::lock_guard l{min_last_epoch_clean_lock};
      const auto elapsed = now - last_sent_beacon;
      if (chrono::duration_cast<chrono::seconds>(elapsed).count() >
        cct->_conf->osd_beacon_report_interval) {
        need_send_beacon = true;
      }
    }
    if (need_send_beacon) {
      send_beacon(now);
    }
  }

  mgrc.update_daemon_health(get_health_metrics());
  service.kick_recovery_queue();
  tick_timer_without_osd_lock.add_event_after(get_tick_interval(),
					      new C_Tick_WithoutOSDLock(this));
}

// Usage:
//   setomapval <pool-id> [namespace/]<obj-name> <key> <val>
//   rmomapkey <pool-id> [namespace/]<obj-name> <key>
//   setomapheader <pool-id> [namespace/]<obj-name> <header>
//   getomap <pool> [namespace/]<obj-name>
//   truncobj <pool-id> [namespace/]<obj-name> <newlen>
//   injectmdataerr [namespace/]<obj-name> [shardid]
//   injectdataerr [namespace/]<obj-name> [shardid]
//
//   set_recovery_delay [utime]
void TestOpsSocketHook::test_ops(OSDService *service, ObjectStore *store,
				 std::string_view command,
				 const cmdmap_t& cmdmap, ostream &ss)
{
  //Test support
  //Support changing the omap on a single osd by using the Admin Socket to
  //directly request the osd make a change.
  if (command == "setomapval" || command == "rmomapkey" ||
      command == "setomapheader" || command == "getomap" ||
      command == "truncobj" || command == "injectmdataerr" ||
      command == "injectdataerr"
    ) {
    pg_t rawpg;
    int64_t pool;
    OSDMapRef curmap = service->get_osdmap();
    int r = -1;

    string poolstr;

    cmd_getval(service->cct, cmdmap, "pool", poolstr);
    pool = curmap->lookup_pg_pool_name(poolstr);
    //If we can't find it by name then maybe id specified
    if (pool < 0 && isdigit(poolstr[0]))
      pool = atoll(poolstr.c_str());
    if (pool < 0) {
      ss << "Invalid pool '" << poolstr << "''";
      return;
    }

    string objname, nspace;
    cmd_getval(service->cct, cmdmap, "objname", objname);
    std::size_t found = objname.find_first_of('/');
    if (found != string::npos) {
      nspace = objname.substr(0, found);
      objname = objname.substr(found+1);
    }
    object_locator_t oloc(pool, nspace);
    r = curmap->object_locator_to_pg(object_t(objname), oloc,  rawpg);

    if (r < 0) {
      ss << "Invalid namespace/objname";
      return;
    }

    int64_t shardid;
    cmd_getval(service->cct, cmdmap, "shardid", shardid, int64_t(shard_id_t::NO_SHARD));
    hobject_t obj(object_t(objname), string(""), CEPH_NOSNAP, rawpg.ps(), pool, nspace);
    ghobject_t gobj(obj, ghobject_t::NO_GEN, shard_id_t(uint8_t(shardid)));
    spg_t pgid(curmap->raw_pg_to_pg(rawpg), shard_id_t(shardid));
    if (curmap->pg_is_ec(rawpg)) {
        if ((command != "injectdataerr") && (command != "injectmdataerr")) {
            ss << "Must not call on ec pool, except injectdataerr or injectmdataerr";
            return;
        }
    }

    ObjectStore::Transaction t;

    if (command == "setomapval") {
      map<string, bufferlist> newattrs;
      bufferlist val;
      string key, valstr;
      cmd_getval(service->cct, cmdmap, "key", key);
      cmd_getval(service->cct, cmdmap, "val", valstr);

      val.append(valstr);
      newattrs[key] = val;
      t.omap_setkeys(coll_t(pgid), ghobject_t(obj), newattrs);
      r = store->queue_transaction(service->meta_ch, std::move(t));
      if (r < 0)
        ss << "error=" << r;
      else
        ss << "ok";
    } else if (command == "rmomapkey") {
      string key;
      set<string> keys;
      cmd_getval(service->cct, cmdmap, "key", key);

      keys.insert(key);
      t.omap_rmkeys(coll_t(pgid), ghobject_t(obj), keys);
      r = store->queue_transaction(service->meta_ch, std::move(t));
      if (r < 0)
        ss << "error=" << r;
      else
        ss << "ok";
    } else if (command == "setomapheader") {
      bufferlist newheader;
      string headerstr;

      cmd_getval(service->cct, cmdmap, "header", headerstr);
      newheader.append(headerstr);
      t.omap_setheader(coll_t(pgid), ghobject_t(obj), newheader);
      r = store->queue_transaction(service->meta_ch, std::move(t));
      if (r < 0)
        ss << "error=" << r;
      else
        ss << "ok";
    } else if (command == "getomap") {
      //Debug: Output entire omap
      bufferlist hdrbl;
      map<string, bufferlist> keyvals;
      auto ch = store->open_collection(coll_t(pgid));
      if (!ch) {
	ss << "unable to open collection for " << pgid;
	r = -ENOENT;
      } else {
	r = store->omap_get(ch, ghobject_t(obj), &hdrbl, &keyvals);
	if (r >= 0) {
          ss << "header=" << string(hdrbl.c_str(), hdrbl.length());
          for (map<string, bufferlist>::iterator it = keyvals.begin();
	       it != keyvals.end(); ++it)
            ss << " key=" << (*it).first << " val="
               << string((*it).second.c_str(), (*it).second.length());
	} else {
          ss << "error=" << r;
	}
      }
    } else if (command == "truncobj") {
      int64_t trunclen;
      cmd_getval(service->cct, cmdmap, "len", trunclen);
      t.truncate(coll_t(pgid), ghobject_t(obj), trunclen);
      r = store->queue_transaction(service->meta_ch, std::move(t));
      if (r < 0)
	ss << "error=" << r;
      else
	ss << "ok";
    } else if (command == "injectdataerr") {
      store->inject_data_error(gobj);
      ss << "ok";
    } else if (command == "injectmdataerr") {
      store->inject_mdata_error(gobj);
      ss << "ok";
    }
    return;
  }
  if (command == "set_recovery_delay") {
    int64_t delay;
    cmd_getval(service->cct, cmdmap, "utime", delay, (int64_t)0);
    ostringstream oss;
    oss << delay;
    int r = service->cct->_conf.set_val("osd_recovery_delay_start",
					 oss.str().c_str());
    if (r != 0) {
      ss << "set_recovery_delay: error setting "
	 << "osd_recovery_delay_start to '" << delay << "': error "
	 << r;
      return;
    }
    service->cct->_conf.apply_changes(nullptr);
    ss << "set_recovery_delay: set osd_recovery_delay_start "
       << "to " << service->cct->_conf->osd_recovery_delay_start;
    return;
  }
  if (command ==  "trigger_scrub" || command == "trigger_deep_scrub") {
    spg_t pgid;
    bool deep = (command == "trigger_deep_scrub");
    OSDMapRef curmap = service->get_osdmap();

    string pgidstr;

    cmd_getval(service->cct, cmdmap, "pgid", pgidstr);
    if (!pgid.parse(pgidstr.c_str())) {
      ss << "Invalid pgid specified";
      return;
    }

    int64_t time;
    cmd_getval(service->cct, cmdmap, "time", time, (int64_t)0);

    PGRef pg = service->osd->_lookup_lock_pg(pgid);
    if (pg == nullptr) {
      ss << "Can't find pg " << pgid;
      return;
    }

    if (pg->is_primary()) {
      const pg_pool_t *p = curmap->get_pg_pool(pgid.pool());
      double pool_scrub_max_interval = 0;
      double scrub_max_interval;
      if (deep) {
        p->opts.get(pool_opts_t::DEEP_SCRUB_INTERVAL, &pool_scrub_max_interval);
        scrub_max_interval = pool_scrub_max_interval > 0 ?
          pool_scrub_max_interval : g_conf()->osd_deep_scrub_interval;
      } else {
        p->opts.get(pool_opts_t::SCRUB_MAX_INTERVAL, &pool_scrub_max_interval);
        scrub_max_interval = pool_scrub_max_interval > 0 ?
          pool_scrub_max_interval : g_conf()->osd_scrub_max_interval;
      }
      // Instead of marking must_scrub force a schedule scrub
      utime_t stamp = ceph_clock_now();
      if (time == 0)
        stamp -= scrub_max_interval;
      else
        stamp -=  (float)time;
      stamp -= 100.0;  // push back last scrub more for good measure
      if (deep) {
        pg->set_last_deep_scrub_stamp(stamp);
      } else {
        pg->set_last_scrub_stamp(stamp);
      }
      ss << "ok - set" << (deep ? " deep" : "" ) << " stamp " << stamp;
    } else {
      ss << "Not primary";
    }
    pg->unlock();
    return;
  }
  if (command == "injectfull") {
    int64_t count;
    string type;
    OSDService::s_names state;
    cmd_getval(service->cct, cmdmap, "type", type, string("full"));
    cmd_getval(service->cct, cmdmap, "count", count, (int64_t)-1);
    if (type == "none" || count == 0) {
      type = "none";
      count = 0;
    }
    state = service->get_full_state(type);
    if (state == OSDService::s_names::INVALID) {
      ss << "Invalid type use (none, nearfull, backfillfull, full, failsafe)";
      return;
    }
    service->set_injectfull(state, count);
    return;
  }
  ss << "Internal error - command=" << command;
}

// =========================================

void OSD::ms_handle_connect(Connection *con)
{
  dout(10) << __func__ << " con " << con << dendl;
  if (con->get_peer_type() == CEPH_ENTITY_TYPE_MON) {
    std::lock_guard l(osd_lock);
    if (is_stopping())
      return;
    dout(10) << __func__ << " on mon" << dendl;

    if (is_preboot()) {
      start_boot();
    } else if (is_booting()) {
      _send_boot();       // resend boot message
    } else {
      map_lock.get_read();
      std::lock_guard l2(mon_report_lock);

      utime_t now = ceph_clock_now();
      last_mon_report = now;

      // resend everything, it's a new session
      send_full_update();
      send_alive();
      service.requeue_pg_temp();
      service.clear_sent_ready_to_merge();
      service.send_pg_temp();
      service.send_ready_to_merge();
      service.send_pg_created();
      requeue_failures();
      send_failures();

      map_lock.put_read();
      if (is_active()) {
	send_beacon(ceph::coarse_mono_clock::now());
      }
    }

    // full map requests may happen while active or pre-boot
    if (requested_full_first) {
      rerequest_full_maps();
    }
  }
}

void OSD::ms_handle_fast_connect(Connection *con)
{
  if (con->get_peer_type() != CEPH_ENTITY_TYPE_MON &&
      con->get_peer_type() != CEPH_ENTITY_TYPE_MGR) {
    auto priv = con->get_priv();
    auto s = static_cast<Session*>(priv.get());
    if (!s) {
      s = new Session{cct, con};
      con->set_priv(RefCountedPtr{s, false});
      dout(10) << " new session (outgoing) " << s << " con=" << s->con
          << " addr=" << s->con->get_peer_addr() << dendl;
      // we don't connect to clients
      ceph_assert(con->get_peer_type() == CEPH_ENTITY_TYPE_OSD);
      s->entity_name.set_type(CEPH_ENTITY_TYPE_OSD);
    }
  }
}

void OSD::ms_handle_fast_accept(Connection *con)
{
  if (con->get_peer_type() != CEPH_ENTITY_TYPE_MON &&
      con->get_peer_type() != CEPH_ENTITY_TYPE_MGR) {
    auto priv = con->get_priv();
    auto s = static_cast<Session*>(priv.get());
    if (!s) {
      s = new Session{cct, con};
      con->set_priv(RefCountedPtr{s, false});
      dout(10) << "new session (incoming)" << s << " con=" << con
          << " addr=" << con->get_peer_addr()
          << " must have raced with connect" << dendl;
      ceph_assert(con->get_peer_type() == CEPH_ENTITY_TYPE_OSD);
      s->entity_name.set_type(CEPH_ENTITY_TYPE_OSD);
    }
  }
}

bool OSD::ms_handle_reset(Connection *con)
{
  auto s = con->get_priv();
  auto session = static_cast<Session*>(s.get());
  dout(2) << "ms_handle_reset con " << con << " session " << session << dendl;
  if (!session)
    return false;
  session->wstate.reset(con);
  session->con->set_priv(nullptr);
  session->con.reset();  // break con <-> session ref cycle
  // note that we break session->con *before* the session_handle_reset
  // cleanup below.  this avoids a race between us and
  // PG::add_backoff, Session::check_backoff, etc.
  session_handle_reset(SessionRef{session});
  return true;
}

bool OSD::ms_handle_refused(Connection *con)
{
  if (!cct->_conf->osd_fast_fail_on_connection_refused)
    return false;

  auto priv = con->get_priv();
  auto session = static_cast<Session*>(priv.get());
  dout(2) << "ms_handle_refused con " << con << " session " << session << dendl;
  if (!session)
    return false;
  int type = con->get_peer_type();
  // handle only OSD failures here
  if (monc && (type == CEPH_ENTITY_TYPE_OSD)) {
    OSDMapRef osdmap = get_osdmap();
    if (osdmap) {
      int id = osdmap->identify_osd_on_all_channels(con->get_peer_addr());
      if (id >= 0 && osdmap->is_up(id)) {
	// I'm cheating mon heartbeat grace logic, because we know it's not going
	// to respawn alone. +1 so we won't hit any boundary case.
	monc->send_mon_message(
	  new MOSDFailure(
	    monc->get_fsid(),
	    id,
	    osdmap->get_addrs(id),
	    cct->_conf->osd_heartbeat_grace + 1,
	    osdmap->get_epoch(),
	    MOSDFailure::FLAG_IMMEDIATE | MOSDFailure::FLAG_FAILED
	    ));
      }
    }
  }
  return true;
}

struct C_OSD_GetVersion : public Context {
  OSD *osd;
  uint64_t oldest, newest;
  explicit C_OSD_GetVersion(OSD *o) : osd(o), oldest(0), newest(0) {}
  void finish(int r) override {
    if (r >= 0)
      osd->_got_mon_epochs(oldest, newest);
  }
};

void OSD::start_boot()
{
  if (!_is_healthy()) {
    // if we are not healthy, do not mark ourselves up (yet)
    dout(1) << "not healthy; waiting to boot" << dendl;
    if (!is_waiting_for_healthy())
      start_waiting_for_healthy();
    // send pings sooner rather than later
    heartbeat_kick();
    return;
  }
  dout(1) << __func__ << dendl;
  set_state(STATE_PREBOOT);
  dout(10) << "start_boot - have maps " << superblock.oldest_map
	   << ".." << superblock.newest_map << dendl;
  C_OSD_GetVersion *c = new C_OSD_GetVersion(this);
  monc->get_version("osdmap", &c->newest, &c->oldest, c);
}

void OSD::_got_mon_epochs(epoch_t oldest, epoch_t newest)
{
  std::lock_guard l(osd_lock);
  if (is_preboot()) {
    _preboot(oldest, newest);
  }
}

void OSD::_preboot(epoch_t oldest, epoch_t newest)
{
  ceph_assert(is_preboot());
  dout(10) << __func__ << " _preboot mon has osdmaps "
	   << oldest << ".." << newest << dendl;

  // ensure our local fullness awareness is accurate
  {
    std::lock_guard l(heartbeat_lock);
    heartbeat();
  }

  const auto& monmap = monc->monmap;

  // if our map within recent history, try to add ourselves to the osdmap.
  if (osdmap->get_epoch() == 0) {
    derr << "waiting for initial osdmap" << dendl;
  } else if (osdmap->is_destroyed(whoami)) {
    derr << "osdmap says I am destroyed" << dendl;
    // provide a small margin so we don't livelock seeing if we
    // un-destroyed ourselves.
    if (osdmap->get_epoch() > newest - 1) {
      exit(0);
    }
  } else if (osdmap->is_noup(whoami)) {
    derr << "osdmap NOUP flag is set, waiting for it to clear" << dendl;
  } else if (!osdmap->test_flag(CEPH_OSDMAP_SORTBITWISE)) {
    derr << "osdmap SORTBITWISE OSDMap flag is NOT set; please set it"
	 << dendl;
  } else if (osdmap->require_osd_release < ceph_release_t::luminous) {
    derr << "osdmap require_osd_release < luminous; please upgrade to luminous"
	 << dendl;
  } else if (service.need_fullness_update()) {
    derr << "osdmap fullness state needs update" << dendl;
    send_full_update();
  } else if (monmap.min_mon_release >= ceph_release_t::octopus &&
	     superblock.purged_snaps_last < superblock.current_epoch) {
    dout(10) << __func__ << " purged_snaps_last " << superblock.purged_snaps_last
	     << " < newest_map " << superblock.current_epoch << dendl;
    _get_purged_snaps();
  } else if (osdmap->get_epoch() >= oldest - 1 &&
	     osdmap->get_epoch() + cct->_conf->osd_map_message_max > newest) {

    // wait for pgs to fully catch up in a different thread, since
    // this thread might be required for splitting and merging PGs to
    // make progress.
    boot_finisher.queue(
      new FunctionContext(
	[this](int r) {
	  std::lock_guard l(osd_lock);
	  if (is_preboot()) {
	    dout(10) << __func__ << " waiting for peering work to drain"
		     << dendl;
	    osd_lock.Unlock();
	    for (auto shard : shards) {
	      shard->wait_min_pg_epoch(osdmap->get_epoch());
	    }
	    osd_lock.Lock();
	  }
	  if (is_preboot()) {
	    _send_boot();
	  }
	}));
    return;
  }

  // get all the latest maps
  if (osdmap->get_epoch() + 1 >= oldest)
    osdmap_subscribe(osdmap->get_epoch() + 1, false);
  else
    osdmap_subscribe(oldest - 1, true);
}

void OSD::_get_purged_snaps()
{
  // NOTE: this is a naive, stateless implementaiton.  it may send multiple
  // overlapping requests to the mon, which will be somewhat inefficient, but
  // it should be reliable.
  dout(10) << __func__ << " purged_snaps_last " << superblock.purged_snaps_last
	   << ", newest_map " << superblock.current_epoch << dendl;
  MMonGetPurgedSnaps *m = new MMonGetPurgedSnaps(
    superblock.purged_snaps_last + 1,
    superblock.current_epoch + 1);
  monc->send_mon_message(m);
}

void OSD::handle_get_purged_snaps_reply(MMonGetPurgedSnapsReply *m)
{
  dout(10) << __func__ << " " << *m << dendl;
  ObjectStore::Transaction t;
  if (!is_preboot() ||
      m->last < superblock.purged_snaps_last) {
    goto out;
  }
  SnapMapper::record_purged_snaps(cct, store, service.meta_ch,
				  make_purged_snaps_oid(), &t,
				  m->purged_snaps);
  superblock.purged_snaps_last = m->last;
  write_superblock(t);
  store->queue_transaction(
    service.meta_ch,
    std::move(t));
  service.publish_superblock(superblock);
  if (m->last < superblock.current_epoch) {
    _get_purged_snaps();
  } else {
    start_boot();
  }
out:
  m->put();
}

void OSD::send_full_update()
{
  if (!service.need_fullness_update())
    return;
  unsigned state = 0;
  if (service.is_full()) {
    state = CEPH_OSD_FULL;
  } else if (service.is_backfillfull()) {
    state = CEPH_OSD_BACKFILLFULL;
  } else if (service.is_nearfull()) {
    state = CEPH_OSD_NEARFULL;
  }
  set<string> s;
  OSDMap::calc_state_set(state, s);
  dout(10) << __func__ << " want state " << s << dendl;
  monc->send_mon_message(new MOSDFull(osdmap->get_epoch(), state));
}

void OSD::start_waiting_for_healthy()
{
  dout(1) << "start_waiting_for_healthy" << dendl;
  set_state(STATE_WAITING_FOR_HEALTHY);
  last_heartbeat_resample = utime_t();

  // subscribe to osdmap updates, in case our peers really are known to be dead
  osdmap_subscribe(osdmap->get_epoch() + 1, false);
}

bool OSD::_is_healthy()
{
  if (!cct->get_heartbeat_map()->is_healthy()) {
    dout(1) << "is_healthy false -- internal heartbeat failed" << dendl;
    return false;
  }

  if (is_waiting_for_healthy()) {
     utime_t now = ceph_clock_now();
     utime_t grace = utime_t(cct->_conf->osd_max_markdown_period, 0);
     while (!osd_markdown_log.empty() &&
             osd_markdown_log.front() + grace < now)
       osd_markdown_log.pop_front();
     if (osd_markdown_log.size() <= 1) {
       dout(5) << __func__ << " first time marked as down,"
               << " try reboot unconditionally" << dendl;
       return true;
    }
    std::lock_guard l(heartbeat_lock);
    int num = 0, up = 0;
    for (map<int,HeartbeatInfo>::iterator p = heartbeat_peers.begin();
	 p != heartbeat_peers.end();
	 ++p) {
      if (p->second.is_healthy(now))
	++up;
      ++num;
    }
    if ((float)up < (float)num * cct->_conf->osd_heartbeat_min_healthy_ratio) {
      dout(1) << "is_healthy false -- only " << up << "/" << num << " up peers (less than "
	      << int(cct->_conf->osd_heartbeat_min_healthy_ratio * 100.0) << "%)" << dendl;
      return false;
    }
  }

  return true;
}

void OSD::_send_boot()
{
  dout(10) << "_send_boot" << dendl;
  Connection *local_connection =
    cluster_messenger->get_loopback_connection().get();
  entity_addrvec_t client_addrs = client_messenger->get_myaddrs();
  entity_addrvec_t cluster_addrs = cluster_messenger->get_myaddrs();
  entity_addrvec_t hb_back_addrs = hb_back_server_messenger->get_myaddrs();
  entity_addrvec_t hb_front_addrs = hb_front_server_messenger->get_myaddrs();

  dout(20) << " initial client_addrs " << client_addrs
	   << ", cluster_addrs " << cluster_addrs
	   << ", hb_back_addrs " << hb_back_addrs
	   << ", hb_front_addrs " << hb_front_addrs
	   << dendl;
  if (cluster_messenger->set_addr_unknowns(client_addrs)) {
    dout(10) << " assuming cluster_addrs match client_addrs "
	     << client_addrs << dendl;
    cluster_addrs = cluster_messenger->get_myaddrs();
  }
  if (auto session = local_connection->get_priv(); !session) {
    cluster_messenger->ms_deliver_handle_fast_connect(local_connection);
  }

  local_connection = hb_back_server_messenger->get_loopback_connection().get();
  if (hb_back_server_messenger->set_addr_unknowns(cluster_addrs)) {
    dout(10) << " assuming hb_back_addrs match cluster_addrs "
	     << cluster_addrs << dendl;
    hb_back_addrs = hb_back_server_messenger->get_myaddrs();
  }
  if (auto session = local_connection->get_priv(); !session) {
    hb_back_server_messenger->ms_deliver_handle_fast_connect(local_connection);
  }

  local_connection = hb_front_server_messenger->get_loopback_connection().get();
  if (hb_front_server_messenger->set_addr_unknowns(client_addrs)) {
    dout(10) << " assuming hb_front_addrs match client_addrs "
	     << client_addrs << dendl;
    hb_front_addrs = hb_front_server_messenger->get_myaddrs();
  }
  if (auto session = local_connection->get_priv(); !session) {
    hb_front_server_messenger->ms_deliver_handle_fast_connect(local_connection);
  }

  // we now know what our front and back addrs will be, and we are
  // about to tell the mon what our metadata (including numa bindings)
  // are, so now is a good time!
  set_numa_affinity();

  MOSDBoot *mboot = new MOSDBoot(
    superblock, get_osdmap_epoch(), service.get_boot_epoch(),
    hb_back_addrs, hb_front_addrs, cluster_addrs,
    CEPH_FEATURES_ALL);
  dout(10) << " final client_addrs " << client_addrs
	   << ", cluster_addrs " << cluster_addrs
	   << ", hb_back_addrs " << hb_back_addrs
	   << ", hb_front_addrs " << hb_front_addrs
	   << dendl;
  _collect_metadata(&mboot->metadata);
  monc->send_mon_message(mboot);
  set_state(STATE_BOOTING);
}

void OSD::_collect_metadata(map<string,string> *pm)
{
  // config info
  (*pm)["osd_data"] = dev_path;
  if (store->get_type() == "filestore") {
    // not applicable for bluestore
    (*pm)["osd_journal"] = journal_path;
  }
  (*pm)["front_addr"] = stringify(client_messenger->get_myaddrs());
  (*pm)["back_addr"] = stringify(cluster_messenger->get_myaddrs());
  (*pm)["hb_front_addr"] = stringify(hb_front_server_messenger->get_myaddrs());
  (*pm)["hb_back_addr"] = stringify(hb_back_server_messenger->get_myaddrs());

  // backend
  (*pm)["osd_objectstore"] = store->get_type();
  (*pm)["rotational"] = store_is_rotational ? "1" : "0";
  (*pm)["journal_rotational"] = journal_is_rotational ? "1" : "0";
  (*pm)["default_device_class"] = store->get_default_device_class();
  store->collect_metadata(pm);

  collect_sys_info(pm, cct);

  (*pm)["front_iface"] = pick_iface(
    cct,
    client_messenger->get_myaddrs().front().get_sockaddr_storage());
  (*pm)["back_iface"] = pick_iface(
    cct,
    cluster_messenger->get_myaddrs().front().get_sockaddr_storage());

  // network numa
  {
    int node = -1;
    set<int> nodes;
    set<string> unknown;
    for (auto nm : { "front_iface", "back_iface" }) {
      if (!(*pm)[nm].size()) {
	unknown.insert(nm);
	continue;
      }
      int n = -1;
      int r = get_iface_numa_node((*pm)[nm], &n);
      if (r < 0) {
	unknown.insert((*pm)[nm]);
	continue;
      }
      nodes.insert(n);
      if (node < 0) {
	node = n;
      }
    }
    if (unknown.size()) {
      (*pm)["network_numa_unknown_ifaces"] = stringify(unknown);
    }
    if (!nodes.empty()) {
      (*pm)["network_numa_nodes"] = stringify(nodes);
    }
    if (node >= 0 && nodes.size() == 1 && unknown.empty()) {
      (*pm)["network_numa_node"] = stringify(node);
    }
  }

  if (numa_node >= 0) {
    (*pm)["numa_node"] = stringify(numa_node);
    (*pm)["numa_node_cpus"] = cpu_set_to_str_list(numa_cpu_set_size,
						  &numa_cpu_set);
  }

  set<string> devnames;
  store->get_devices(&devnames);
  (*pm)["devices"] = stringify(devnames);
  string devids;
  for (auto& dev : devnames) {
    string err;
    string id = get_device_id(dev, &err);
    if (id.size()) {
      if (!devids.empty()) {
	devids += ",";
      }
      devids += dev + "=" + id;
    } else {
      dout(10) << __func__ << " no unique device id for " << dev << ": "
	       << err << dendl;
    }
  }
  (*pm)["device_ids"] = devids;

  dout(10) << __func__ << " " << *pm << dendl;
}

void OSD::queue_want_up_thru(epoch_t want)
{
  map_lock.get_read();
  epoch_t cur = osdmap->get_up_thru(whoami);
  std::lock_guard l(mon_report_lock);
  if (want > up_thru_wanted) {
    dout(10) << "queue_want_up_thru now " << want << " (was " << up_thru_wanted << ")"
	     << ", currently " << cur
	     << dendl;
    up_thru_wanted = want;
    send_alive();
  } else {
    dout(10) << "queue_want_up_thru want " << want << " <= queued " << up_thru_wanted
	     << ", currently " << cur
	     << dendl;
  }
  map_lock.put_read();
}

void OSD::send_alive()
{
  ceph_assert(mon_report_lock.is_locked());
  if (!osdmap->exists(whoami))
    return;
  epoch_t up_thru = osdmap->get_up_thru(whoami);
  dout(10) << "send_alive up_thru currently " << up_thru << " want " << up_thru_wanted << dendl;
  if (up_thru_wanted > up_thru) {
    dout(10) << "send_alive want " << up_thru_wanted << dendl;
    monc->send_mon_message(new MOSDAlive(osdmap->get_epoch(), up_thru_wanted));
  }
}

void OSD::request_full_map(epoch_t first, epoch_t last)
{
  dout(10) << __func__ << " " << first << ".." << last
	   << ", previously requested "
	   << requested_full_first << ".." << requested_full_last << dendl;
  ceph_assert(osd_lock.is_locked());
  ceph_assert(first > 0 && last > 0);
  ceph_assert(first <= last);
  ceph_assert(first >= requested_full_first);  // we shouldn't ever ask for older maps
  if (requested_full_first == 0) {
    // first request
    requested_full_first = first;
    requested_full_last = last;
  } else if (last <= requested_full_last) {
    // dup
    return;
  } else {
    // additional request
    first = requested_full_last + 1;
    requested_full_last = last;
  }
  MMonGetOSDMap *req = new MMonGetOSDMap;
  req->request_full(first, last);
  monc->send_mon_message(req);
}

void OSD::got_full_map(epoch_t e)
{
  ceph_assert(requested_full_first <= requested_full_last);
  ceph_assert(osd_lock.is_locked());
  if (requested_full_first == 0) {
    dout(20) << __func__ << " " << e << ", nothing requested" << dendl;
    return;
  }
  if (e < requested_full_first) {
    dout(10) << __func__ << " " << e << ", requested " << requested_full_first
	     << ".." << requested_full_last
	     << ", ignoring" << dendl;
    return;
  }
  if (e >= requested_full_last) {
    dout(10) << __func__ << " " << e << ", requested " << requested_full_first
	     << ".." << requested_full_last << ", resetting" << dendl;
    requested_full_first = requested_full_last = 0;
    return;
  }
  
  requested_full_first = e + 1;

  dout(10) << __func__ << " " << e << ", requested " << requested_full_first
           << ".." << requested_full_last
           << ", still need more" << dendl;
}

void OSD::requeue_failures()
{
  std::lock_guard l(heartbeat_lock);
  unsigned old_queue = failure_queue.size();
  unsigned old_pending = failure_pending.size();
  for (auto p = failure_pending.begin(); p != failure_pending.end(); ) {
    failure_queue[p->first] = p->second.first;
    failure_pending.erase(p++);
  }
  dout(10) << __func__ << " " << old_queue << " + " << old_pending << " -> "
	   << failure_queue.size() << dendl;
}

void OSD::send_failures()
{
  ceph_assert(map_lock.is_locked());
  ceph_assert(mon_report_lock.is_locked());
  std::lock_guard l(heartbeat_lock);
  utime_t now = ceph_clock_now();
  while (!failure_queue.empty()) {
    int osd = failure_queue.begin()->first;
    if (!failure_pending.count(osd)) {
      int failed_for = (int)(double)(now - failure_queue.begin()->second);
      monc->send_mon_message(
	new MOSDFailure(
	  monc->get_fsid(),
	  osd,
	  osdmap->get_addrs(osd),
	  failed_for,
	  osdmap->get_epoch()));
      failure_pending[osd] = make_pair(failure_queue.begin()->second,
				       osdmap->get_addrs(osd));
    }
    failure_queue.erase(osd);
  }
}

void OSD::send_still_alive(epoch_t epoch, int osd, const entity_addrvec_t &addrs)
{
  MOSDFailure *m = new MOSDFailure(monc->get_fsid(), osd, addrs, 0, epoch,
				   MOSDFailure::FLAG_ALIVE);
  monc->send_mon_message(m);
}

void OSD::cancel_pending_failures()
{
  std::lock_guard l(heartbeat_lock);
  auto it = failure_pending.begin();
  while (it != failure_pending.end()) {
    dout(10) << __func__ << " canceling in-flight failure report for osd."
             << it->first << dendl;
    send_still_alive(osdmap->get_epoch(), it->first, it->second.second);
    failure_pending.erase(it++);
  }
}

void OSD::send_beacon(const ceph::coarse_mono_clock::time_point& now)
{
  const auto& monmap = monc->monmap;
  // send beacon to mon even if we are just connected, and the monmap is not
  // initialized yet by then.
  if (monmap.epoch > 0 &&
      monmap.get_required_features().contains_all(
        ceph::features::mon::FEATURE_LUMINOUS)) {
    dout(20) << __func__ << " sending" << dendl;
    MOSDBeacon* beacon = nullptr;
    {
      std::lock_guard l{min_last_epoch_clean_lock};
      beacon = new MOSDBeacon(osdmap->get_epoch(),
			      min_last_epoch_clean,
			      superblock.last_purged_snaps_scrub);
      beacon->pgs = min_last_epoch_clean_pgs;
      last_sent_beacon = now;
    }
    monc->send_mon_message(beacon);
  } else {
    dout(20) << __func__ << " not sending" << dendl;
  }
}

void OSD::handle_command(MMonCommand *m)
{
  if (!require_mon_peer(m)) {
    m->put();
    return;
  }

  Command *c = new Command(m->cmd, m->get_tid(), m->get_data(), NULL);
  command_wq.queue(c);
  m->put();
}

void OSD::handle_command(MCommand *m)
{
  ConnectionRef con = m->get_connection();
  auto priv = con->get_priv();
  auto session = static_cast<Session *>(priv.get());
  if (!session) {
    con->send_message(new MCommandReply(m, -EPERM));
    m->put();
    return;
  }

  OSDCap& caps = session->caps;
  priv.reset();

  if (!caps.allow_all() || m->get_source().is_mon()) {
    con->send_message(new MCommandReply(m, -EPERM));
    m->put();
    return;
  }

  Command *c = new Command(m->cmd, m->get_tid(), m->get_data(), con.get());
  command_wq.queue(c);

  m->put();
}

struct OSDCommand {
  string cmdstring;
  string helpstring;
  string module;
  string perm;
} osd_commands[] = {

#define COMMAND(parsesig, helptext, module, perm) \
  {parsesig, helptext, module, perm},

// yes, these are really pg commands, but there's a limit to how
// much work it's worth.  The OSD returns all of them.  Make this
// form (pg <pgid> <cmd>) valid only for the cli.
// Rest uses "tell <pgid> <cmd>"

COMMAND("pg " \
	"name=pgid,type=CephPgid " \
	"name=cmd,type=CephChoices,strings=query", \
	"show details of a specific pg", "osd", "r")
COMMAND("pg " \
	"name=pgid,type=CephPgid " \
	"name=cmd,type=CephChoices,strings=mark_unfound_lost " \
	"name=mulcmd,type=CephChoices,strings=revert|delete", \
	"mark all unfound objects in this pg as lost, either removing or reverting to a prior version if one is available",
	"osd", "rw")
COMMAND("pg " \
	"name=pgid,type=CephPgid " \
	"name=cmd,type=CephChoices,strings=list_unfound " \
	"name=offset,type=CephString,req=false",
	"list unfound objects on this pg, perhaps starting at an offset given in JSON",
	"osd", "r")

// new form: tell <pgid> <cmd> for both cli and rest

COMMAND("query",
	"show details of a specific pg", "osd", "r")
COMMAND("mark_unfound_lost " \
	"name=mulcmd,type=CephChoices,strings=revert|delete", \
	"mark all unfound objects in this pg as lost, either removing or reverting to a prior version if one is available",
	"osd", "rw")
COMMAND("list_unfound " \
	"name=offset,type=CephString,req=false",
	"list unfound objects on this pg, perhaps starting at an offset given in JSON",
	"osd", "r")
COMMAND("perf histogram dump "
        "name=logger,type=CephString,req=false "
        "name=counter,type=CephString,req=false",
	"Get histogram data",
	"osd", "r")

// tell <osd.n> commands.  Validation of osd.n must be special-cased in client
COMMAND("version", "report version of OSD", "osd", "r")
COMMAND("get_command_descriptions", "list commands descriptions", "osd", "r")
COMMAND("injectargs " \
	"name=injected_args,type=CephString,n=N",
	"inject configuration arguments into running OSD",
	"osd", "rw")
COMMAND("config set " \
	"name=key,type=CephString name=value,type=CephString",
	"Set a configuration option at runtime (not persistent)",
	"osd", "rw")
COMMAND("config get " \
	"name=key,type=CephString",
	"Get a configuration option at runtime",
	"osd", "r")
COMMAND("config unset " \
	"name=key,type=CephString",
	"Unset a configuration option at runtime (not persistent)",
	"osd", "rw")
COMMAND("cluster_log " \
	"name=level,type=CephChoices,strings=error,warning,info,debug " \
	"name=message,type=CephString,n=N",
	"log a message to the cluster log",
	"osd", "rw")
COMMAND("bench " \
	"name=count,type=CephInt,req=false " \
	"name=size,type=CephInt,req=false " \
	"name=object_size,type=CephInt,req=false " \
	"name=object_num,type=CephInt,req=false ", \
	"OSD benchmark: write <count> <size>-byte objects(with <obj_size> <obj_num>), " \
	"(default count=1G default size=4MB). Results in log.",
	"osd", "rw")
COMMAND("flush_pg_stats", "flush pg stats", "osd", "rw")
COMMAND("heap " \
	"name=heapcmd,type=CephChoices,strings="\
	    "dump|start_profiler|stop_profiler|release|get_release_rate|set_release_rate|stats " \
	"name=value,type=CephString,req=false",
	"show heap usage info (available only if compiled with tcmalloc)",
	"osd", "rw")
COMMAND("debug dump_missing " \
	"name=filename,type=CephFilepath",
	"dump missing objects to a named file", "osd", "r")
COMMAND("debug kick_recovery_wq " \
	"name=delay,type=CephInt,range=0",
	"set osd_recovery_delay_start to <val>", "osd", "rw")
COMMAND("cpu_profiler " \
	"name=arg,type=CephChoices,strings=status|flush",
	"run cpu profiling on daemon", "osd", "rw")
COMMAND("dump_pg_recovery_stats", "dump pg recovery statistics",
	"osd", "r")
COMMAND("reset_pg_recovery_stats", "reset pg recovery statistics",
	"osd", "rw")
COMMAND("compact",
        "compact object store's omap. "
        "WARNING: Compaction probably slows your requests",
        "osd", "rw")
COMMAND("smart name=devid,type=CephString,req=False",
        "runs smartctl on this osd devices.  ",
        "osd", "rw")
COMMAND("cache drop",
        "Drop all OSD caches",
        "osd", "rwx")
COMMAND("cache status",
        "Get OSD caches statistics",
        "osd", "r")
COMMAND("send_beacon",
        "Send OSD beacon to mon immediately",
        "osd", "r")
COMMAND("scrub_purged_snaps",
	"Scrub purged_snaps vs snapmapper index",
	"osd", "r")
};

void OSD::do_command(
  Connection *con, ceph_tid_t tid, vector<string>& cmd, bufferlist& data)
{
  dout(20) << "do_command tid " << tid << " " << cmd << dendl;

  int r = 0;
  stringstream ss, ds;
  bufferlist odata;
  cmdmap_t cmdmap;
  if (cmd.empty()) {
    ss << "no command given";
    goto out;
  }
  if (!cmdmap_from_json(cmd, &cmdmap, ss)) {
    r = -EINVAL;
    goto out;
  }

  try {
    r = _do_command(con, cmdmap, tid, data, odata, ss, ds);
  } catch (const bad_cmd_get& e) {
    r = -EINVAL;
    ss << e.what();
  }
  if (r == -EAGAIN) {
    return;
  }
 out:
  string rs = ss.str();
  odata.append(ds);
  dout(0) << "do_command r=" << r << " " << rs << dendl;
  clog->info() << rs;
  if (con) {
    MCommandReply *reply = new MCommandReply(r, rs);
    reply->set_tid(tid);
    reply->set_data(odata);
    con->send_message(reply);
  }
}

namespace {
  class unlock_guard {
    Mutex& m;
  public:
    explicit unlock_guard(Mutex& mutex)
      : m(mutex)
    {
      m.unlock();
    }
    unlock_guard(unlock_guard&) = delete;
    ~unlock_guard() {
      m.lock();
    }
  };
}

int OSD::_do_command(
  Connection *con, cmdmap_t& cmdmap, ceph_tid_t tid, bufferlist& data,
  bufferlist& odata, stringstream& ss, stringstream& ds)
{
  int r = 0;
  string prefix;
  string format;
  string pgidstr;
  boost::scoped_ptr<Formatter> f;

  cmd_getval(cct, cmdmap, "prefix", prefix);

  if (prefix == "get_command_descriptions") {
    int cmdnum = 0;
    JSONFormatter *f = new JSONFormatter();
    f->open_object_section("command_descriptions");
    for (OSDCommand *cp = osd_commands;
	 cp < &osd_commands[std::size(osd_commands)]; cp++) {

      ostringstream secname;
      secname << "cmd" << setfill('0') << std::setw(3) << cmdnum;
      dump_cmddesc_to_json(f, con->get_features(),
                           secname.str(), cp->cmdstring, cp->helpstring,
			   cp->module, cp->perm, 0);
      cmdnum++;
    }
    f->close_section();	// command_descriptions

    f->flush(ds);
    delete f;
    goto out;
  }

  cmd_getval(cct, cmdmap, "format", format);
  f.reset(Formatter::create(format));

  if (prefix == "version") {
    if (f) {
      f->open_object_section("version");
      f->dump_string("version", pretty_version_to_str());
      f->close_section();
      f->flush(ds);
    } else {
      ds << pretty_version_to_str();
    }
    goto out;
  }
  else if (prefix == "injectargs") {
    vector<string> argsvec;
    cmd_getval(cct, cmdmap, "injected_args", argsvec);

    if (argsvec.empty()) {
      r = -EINVAL;
      ss << "ignoring empty injectargs";
      goto out;
    }
    string args = argsvec.front();
    for (vector<string>::iterator a = ++argsvec.begin(); a != argsvec.end(); ++a)
      args += " " + *a;
    unlock_guard unlock{osd_lock};
    r = cct->_conf.injectargs(args, &ss);
  }
  else if (prefix == "config set") {
    std::string key;
    std::string val;
    cmd_getval(cct, cmdmap, "key", key);
    cmd_getval(cct, cmdmap, "value", val);
    unlock_guard unlock{osd_lock};
    r = cct->_conf.set_val(key, val, &ss);
    if (r == 0) {
      cct->_conf.apply_changes(nullptr);
    }
  }
  else if (prefix == "config get") {
    std::string key;
    cmd_getval(cct, cmdmap, "key", key);
    unlock_guard unlock{osd_lock};
    std::string val;
    r = cct->_conf.get_val(key, &val);
    if (r == 0) {
      ds << val;
    }
  }
  else if (prefix == "config unset") {
    std::string key;
    cmd_getval(cct, cmdmap, "key", key);
    unlock_guard unlock{osd_lock};
    r = cct->_conf.rm_val(key);
    if (r == 0) {
      cct->_conf.apply_changes(nullptr);
    }
    if (r == -ENOENT) {
      r = 0;  // make command idempotent
    }
  }
  else if (prefix == "cluster_log") {
    vector<string> msg;
    cmd_getval(cct, cmdmap, "message", msg);
    if (msg.empty()) {
      r = -EINVAL;
      ss << "ignoring empty log message";
      goto out;
    }
    string message = msg.front();
    for (vector<string>::iterator a = ++msg.begin(); a != msg.end(); ++a)
      message += " " + *a;
    string lvl;
    cmd_getval(cct, cmdmap, "level", lvl);
    clog_type level = string_to_clog_type(lvl);
    if (level < 0) {
      r = -EINVAL;
      ss << "unknown level '" << lvl << "'";
      goto out;
    }
    clog->do_log(level, message);
  }

  // either 'pg <pgid> <command>' or
  // 'tell <pgid>' (which comes in without any of that prefix)?

  else if (prefix == "pg" ||
	    prefix == "query" ||
	    prefix == "mark_unfound_lost" ||
	    prefix == "list_unfound"
	   ) {
    pg_t pgid;

    if (!cmd_getval(cct, cmdmap, "pgid", pgidstr)) {
      ss << "no pgid specified";
      r = -EINVAL;
    } else if (!pgid.parse(pgidstr.c_str())) {
      ss << "couldn't parse pgid '" << pgidstr << "'";
      r = -EINVAL;
    } else {
      spg_t pcand;
      PGRef pg;
      if (osdmap->get_primary_shard(pgid, &pcand) &&
	  (pg = _lookup_lock_pg(pcand))) {
	if (pg->is_primary()) {
	  // simulate pg <pgid> cmd= for pg->do-command
	  if (prefix != "pg")
	    cmd_putval(cct, cmdmap, "cmd", prefix);
	  try {
	    r = pg->do_command(cmdmap, ss, data, odata, con, tid);
	  } catch (const bad_cmd_get& e) {
	    pg->unlock();
	    ss << e.what();
	    return -EINVAL;
	  }
	  if (r == -EAGAIN) {
	    pg->unlock();
	    // don't reply, pg will do so async
	    return -EAGAIN;
	  }
	} else {
	  ss << "not primary for pgid " << pgid;

	  // send them the latest diff to ensure they realize the mapping
	  // has changed.
	  service.send_incremental_map(osdmap->get_epoch() - 1, con, osdmap);

	  // do not reply; they will get newer maps and realize they
	  // need to resend.
	  pg->unlock();
	  return -EAGAIN;
	}
	pg->unlock();
      } else {
	ss << "i don't have pgid " << pgid;
	r = -ENOENT;
      }
    }
  }

  else if (prefix == "bench") {
    int64_t count;
    int64_t bsize;
    int64_t osize, onum;
    // default count 1G, size 4MB
    cmd_getval(cct, cmdmap, "count", count, (int64_t)1 << 30);
    cmd_getval(cct, cmdmap, "size", bsize, (int64_t)4 << 20);
    cmd_getval(cct, cmdmap, "object_size", osize, (int64_t)0);
    cmd_getval(cct, cmdmap, "object_num", onum, (int64_t)0);

    uint32_t duration = cct->_conf->osd_bench_duration;

    if (bsize > (int64_t) cct->_conf->osd_bench_max_block_size) {
      // let us limit the block size because the next checks rely on it
      // having a sane value.  If we allow any block size to be set things
      // can still go sideways.
      ss << "block 'size' values are capped at "
         << byte_u_t(cct->_conf->osd_bench_max_block_size) << ". If you wish to use"
         << " a higher value, please adjust 'osd_bench_max_block_size'";
      r = -EINVAL;
      goto out;
    } else if (bsize < (int64_t) (1 << 20)) {
      // entering the realm of small block sizes.
      // limit the count to a sane value, assuming a configurable amount of
      // IOPS and duration, so that the OSD doesn't get hung up on this,
      // preventing timeouts from going off
      int64_t max_count =
        bsize * duration * cct->_conf->osd_bench_small_size_max_iops;
      if (count > max_count) {
        ss << "'count' values greater than " << max_count
           << " for a block size of " << byte_u_t(bsize) << ", assuming "
           << cct->_conf->osd_bench_small_size_max_iops << " IOPS,"
           << " for " << duration << " seconds,"
           << " can cause ill effects on osd. "
           << " Please adjust 'osd_bench_small_size_max_iops' with a higher"
           << " value if you wish to use a higher 'count'.";
        r = -EINVAL;
        goto out;
      }
    } else {
      // 1MB block sizes are big enough so that we get more stuff done.
      // However, to avoid the osd from getting hung on this and having
      // timers being triggered, we are going to limit the count assuming
      // a configurable throughput and duration.
      // NOTE: max_count is the total amount of bytes that we believe we
      //       will be able to write during 'duration' for the given
      //       throughput.  The block size hardly impacts this unless it's
      //       way too big.  Given we already check how big the block size
      //       is, it's safe to assume everything will check out.
      int64_t max_count =
        cct->_conf->osd_bench_large_size_max_throughput * duration;
      if (count > max_count) {
        ss << "'count' values greater than " << max_count
           << " for a block size of " << byte_u_t(bsize) << ", assuming "
           << byte_u_t(cct->_conf->osd_bench_large_size_max_throughput) << "/s,"
           << " for " << duration << " seconds,"
           << " can cause ill effects on osd. "
           << " Please adjust 'osd_bench_large_size_max_throughput'"
           << " with a higher value if you wish to use a higher 'count'.";
        r = -EINVAL;
        goto out;
      }
    }

    if (osize && bsize > osize)
      bsize = osize;

    dout(1) << " bench count " << count
            << " bsize " << byte_u_t(bsize) << dendl;

    ObjectStore::Transaction cleanupt;

    if (osize && onum) {
      bufferlist bl;
      bufferptr bp(osize);
      bp.zero();
      bl.push_back(std::move(bp));
      bl.rebuild_page_aligned();
      for (int i=0; i<onum; ++i) {
	char nm[30];
	snprintf(nm, sizeof(nm), "disk_bw_test_%d", i);
	object_t oid(nm);
	hobject_t soid(sobject_t(oid, 0));
	ObjectStore::Transaction t;
	t.write(coll_t(), ghobject_t(soid), 0, osize, bl);
	store->queue_transaction(service.meta_ch, std::move(t), NULL);
	cleanupt.remove(coll_t(), ghobject_t(soid));
      }
    }

    bufferlist bl;
    bufferptr bp(bsize);
    bp.zero();
    bl.push_back(std::move(bp));
    bl.rebuild_page_aligned();

    {
      C_SaferCond waiter;
      if (!service.meta_ch->flush_commit(&waiter)) {
	waiter.wait();
      }
    }

    utime_t start = ceph_clock_now();
    for (int64_t pos = 0; pos < count; pos += bsize) {
      char nm[30];
      unsigned offset = 0;
      if (onum && osize) {
	snprintf(nm, sizeof(nm), "disk_bw_test_%d", (int)(rand() % onum));
	offset = rand() % (osize / bsize) * bsize;
      } else {
	snprintf(nm, sizeof(nm), "disk_bw_test_%lld", (long long)pos);
      }
      object_t oid(nm);
      hobject_t soid(sobject_t(oid, 0));
      ObjectStore::Transaction t;
      t.write(coll_t::meta(), ghobject_t(soid), offset, bsize, bl);
      store->queue_transaction(service.meta_ch, std::move(t), NULL);
      if (!onum || !osize)
	cleanupt.remove(coll_t::meta(), ghobject_t(soid));
    }

    {
      C_SaferCond waiter;
      if (!service.meta_ch->flush_commit(&waiter)) {
	waiter.wait();
      }
    }
    utime_t end = ceph_clock_now();

    // clean up
    store->queue_transaction(service.meta_ch, std::move(cleanupt), NULL);
    {
      C_SaferCond waiter;
      if (!service.meta_ch->flush_commit(&waiter)) {
	waiter.wait();
      }
    }

    double elapsed = end - start;
    double rate = count / elapsed;
    double iops = rate / bsize;
    if (f) {
      f->open_object_section("osd_bench_results");
      f->dump_int("bytes_written", count);
      f->dump_int("blocksize", bsize);
      f->dump_float("elapsed_sec", elapsed);
      f->dump_float("bytes_per_sec", rate);
      f->dump_float("iops", iops);
      f->close_section();
      f->flush(ds);
    } else {
      ds << "bench: wrote " << byte_u_t(count)
	 << " in blocks of " << byte_u_t(bsize) << " in "
	 << elapsed << " sec at " << byte_u_t(rate) << "/sec "
	 << si_u_t(iops) << " IOPS";
    }
  }

  else if (prefix == "flush_pg_stats") {
    mgrc.send_pgstats();
    ds << service.get_osd_stat_seq() << "\n";
  }

  else if (prefix == "heap") {
    r = ceph::osd_cmds::heap(*cct, cmdmap, *f, ds);
  }

  else if (prefix == "debug dump_missing") {
    if (!f) {
      f.reset(new JSONFormatter(true));
    }
    f->open_array_section("pgs");
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto& pg : pgs) {
      string s = stringify(pg->pg_id);
      f->open_array_section(s.c_str());
      pg->lock();
      pg->dump_missing(f.get());
      pg->unlock();
      f->close_section();
    }
    f->close_section();
    f->flush(ds);
  }
  else if (prefix == "debug kick_recovery_wq") {
    int64_t delay;
    cmd_getval(cct, cmdmap, "delay", delay);
    ostringstream oss;
    oss << delay;
    unlock_guard unlock{osd_lock};
    r = cct->_conf.set_val("osd_recovery_delay_start", oss.str().c_str());
    if (r != 0) {
      ss << "kick_recovery_wq: error setting "
	 << "osd_recovery_delay_start to '" << delay << "': error "
	 << r;
      goto out;
    }
    cct->_conf.apply_changes(nullptr);
    ss << "kicking recovery queue. set osd_recovery_delay_start "
       << "to " << cct->_conf->osd_recovery_delay_start;
  }

  else if (prefix == "cpu_profiler") {
    string arg;
    cmd_getval(cct, cmdmap, "arg", arg);
    vector<string> argvec;
    get_str_vec(arg, argvec);
    cpu_profiler_handle_command(argvec, ds);
  }

  else if (prefix == "dump_pg_recovery_stats") {
    stringstream s;
    if (f) {
      pg_recovery_stats.dump_formatted(f.get());
      f->flush(ds);
    } else {
      pg_recovery_stats.dump(s);
      ds << "dump pg recovery stats: " << s.str();
    }
  }

  else if (prefix == "reset_pg_recovery_stats") {
    ss << "reset pg recovery stats";
    pg_recovery_stats.reset();
  }

  else if (prefix == "perf histogram dump") {
    std::string logger;
    std::string counter;
    cmd_getval(cct, cmdmap, "logger", logger);
    cmd_getval(cct, cmdmap, "counter", counter);
    if (f) {
      cct->get_perfcounters_collection()->dump_formatted_histograms(
          f.get(), false, logger, counter);
      f->flush(ds);
    }
  }

  else if (prefix == "compact") {
    dout(1) << "triggering manual compaction" << dendl;
    auto start = ceph::coarse_mono_clock::now();
    store->compact();
    auto end = ceph::coarse_mono_clock::now();
    double duration = std::chrono::duration<double>(end-start).count();
    dout(1) << "finished manual compaction in "
            << duration
            << " seconds" << dendl;
    ss << "compacted omap in " << duration << " seconds";
  }

  else if (prefix == "smart") {
    string devid;
    cmd_getval(cct, cmdmap, "devid", devid);
    probe_smart(devid, ds);
  }

  else if (prefix == "cache drop") {
    dout(20) << "clearing all caches" << dendl;
    // Clear the objectstore's cache - onode and buffer for Bluestore,
    // system's pagecache for Filestore
    r = store->flush_cache(&ss);
    if (r < 0) {
      ds << "Error flushing objectstore cache: " << cpp_strerror(r);
      goto out;
    }
    // Clear the objectcontext cache (per PG)
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto& pg: pgs) {
      pg->clear_cache();
    }
  }

  else if (prefix == "cache status") {
    int obj_ctx_count = 0;
    vector<PGRef> pgs;
    _get_pgs(&pgs);
    for (auto& pg: pgs) {
      obj_ctx_count += pg->get_cache_obj_count();
    }
    if (f) {
      f->open_object_section("cache_status");
      f->dump_int("object_ctx", obj_ctx_count);
      store->dump_cache_stats(f.get());
      f->close_section();
      f->flush(ds);
    } else {
      ds << "object_ctx: " << obj_ctx_count;
      store->dump_cache_stats(ds);
    }
  }
  else if (prefix == "send_beacon") {
    if (is_active()) {
      send_beacon(ceph::coarse_mono_clock::now());
    }
  } else if (prefix == "scrub_purged_snaps") {
    scrub_purged_snaps();
  } else {
    ss << "unrecognized command '" << prefix << "'";
    r = -EINVAL;
  }

 out:
  return r;
}

void OSD::scrub_purged_snaps()
{
  dout(10) << __func__ << dendl;
  ceph_assert(ceph_mutex_is_locked(osd_lock));
  SnapMapper::Scrubber s(cct, store, service.meta_ch,
			 make_snapmapper_oid(),
			 make_purged_snaps_oid());
  clog->debug() << "purged_snaps scrub starts";
  osd_lock.unlock();
  s.run();
  if (s.stray.size()) {
    clog->debug() << "purged_snaps scrub found " << s.stray.size() << " strays";
  } else {
    clog->debug() << "purged_snaps scrub ok";
  }
  set<pair<spg_t,snapid_t>> queued;
  for (auto& [pool, snap, hash, shard] : s.stray) {
    const pg_pool_t *pi = get_osdmap()->get_pg_pool(pool);
    if (!pi) {
      dout(20) << __func__ << " pool " << pool << " dne" << dendl;
      continue;
    }
    pg_t pgid(pi->raw_hash_to_pg(hash), pool);
    spg_t spgid(pgid, shard);
    pair<spg_t,snapid_t> p(spgid, snap);
    if (queued.count(p)) {
      dout(20) << __func__ << " pg " << spgid << " snap " << snap
	       << " already queued" << dendl;
      continue;
    }
    PGRef pg = lookup_lock_pg(spgid);
    if (!pg) {
      dout(20) << __func__ << " pg " << spgid << " not found" << dendl;
      continue;
    }
    queued.insert(p);
    dout(10) << __func__ << " requeue pg " << spgid << " " << pg << " snap "
	     << snap << dendl;
    pg->queue_snap_retrim(snap);
    pg->unlock();
  }
  osd_lock.Lock();
  if (is_stopping()) {
    return;
  }
  dout(10) << __func__ << " done queueing pgs, updating superblock" << dendl;
  ObjectStore::Transaction t;
  superblock.last_purged_snaps_scrub = ceph_clock_now();
  write_superblock(t);
  int tr = store->queue_transaction(service.meta_ch, std::move(t), nullptr);
  ceph_assert(tr == 0);
  if (is_active()) {
    send_beacon(ceph::coarse_mono_clock::now());
  }
  dout(10) << __func__ << " done" << dendl;
}

void OSD::probe_smart(const string& only_devid, ostream& ss)
{
  set<string> devnames;
  store->get_devices(&devnames);
  uint64_t smart_timeout = cct->_conf.get_val<uint64_t>(
    "osd_smart_report_timeout");

  // == typedef std::map<std::string, mValue> mObject;
  json_spirit::mObject json_map;

  for (auto dev : devnames) {
    // smartctl works only on physical devices; filter out any logical device
    if (dev.find("dm-") == 0) {
      continue;
    }

    string err;
    string devid = get_device_id(dev, &err);
    if (devid.size() == 0) {
      dout(10) << __func__ << " no unique id for dev " << dev << " ("
	       << err << "), skipping" << dendl;
      continue;
    }
    if (only_devid.size() && devid != only_devid) {
      continue;
    }

    json_spirit::mValue smart_json;
    if (block_device_get_metrics(dev, smart_timeout,
				 &smart_json)) {
      dout(10) << "block_device_get_metrics failed for /dev/" << dev << dendl;
      continue;
    }
    json_map[devid] = smart_json;
  }
  json_spirit::write(json_map, ss, json_spirit::pretty_print);
}

bool OSD::heartbeat_dispatch(Message *m)
{
  dout(30) << "heartbeat_dispatch " << m << dendl;
  switch (m->get_type()) {

  case CEPH_MSG_PING:
    dout(10) << "ping from " << m->get_source_inst() << dendl;
    m->put();
    break;

  case MSG_OSD_PING:
    handle_osd_ping(static_cast<MOSDPing*>(m));
    break;

  default:
    dout(0) << "dropping unexpected message " << *m << " from " << m->get_source_inst() << dendl;
    m->put();
  }

  return true;
}

bool OSD::ms_dispatch(Message *m)
{
  dout(20) << "OSD::ms_dispatch: " << *m << dendl;
  if (m->get_type() == MSG_OSD_MARK_ME_DOWN) {
    service.got_stop_ack();
    m->put();
    return true;
  }

  // lock!

  osd_lock.Lock();
  if (is_stopping()) {
    osd_lock.Unlock();
    m->put();
    return true;
  }

  do_waiters();
  _dispatch(m);

  osd_lock.Unlock();

  return true;
}

void OSDService::maybe_share_map(
  Connection *con,
  const OSDMapRef& osdmap,
  epoch_t peer_epoch_lb)
{
  // NOTE: we assume caller hold something that keeps the Connection itself
  // pinned (e.g., an OpRequest's MessageRef).
  auto priv = con->get_priv();
  auto session = static_cast<Session*>(priv.get());
  if (!session) {
    return;
  }

  // assume the peer has the newer of the op's sent_epoch and what
  // we think we sent them.
  session->sent_epoch_lock.lock();
  if (peer_epoch_lb > session->last_sent_epoch) {
    dout(10) << __func__ << " con " << con
	     << " " << con->get_peer_addr()
	     << " map epoch " << session->last_sent_epoch
	     << " -> " << peer_epoch_lb << " (as per caller)" << dendl;
    session->last_sent_epoch = peer_epoch_lb;
  }
  epoch_t last_sent_epoch = session->last_sent_epoch;
  session->sent_epoch_lock.unlock();

  if (osdmap->get_epoch() <= last_sent_epoch) {
    return;
  }

  send_incremental_map(last_sent_epoch, con, osdmap);
  last_sent_epoch = osdmap->get_epoch();

  session->sent_epoch_lock.lock();
  if (session->last_sent_epoch < last_sent_epoch) {
    dout(10) << __func__ << " con " << con
	     << " " << con->get_peer_addr()
	     << " map epoch " << session->last_sent_epoch
	     << " -> " << last_sent_epoch << " (shared)" << dendl;
    session->last_sent_epoch = last_sent_epoch;
  }
  session->sent_epoch_lock.unlock();
}

void OSD::dispatch_session_waiting(SessionRef session, OSDMapRef osdmap)
{
  ceph_assert(session->session_dispatch_lock.is_locked());

  auto i = session->waiting_on_map.begin();
  while (i != session->waiting_on_map.end()) {
    OpRequestRef op = &(*i);
    ceph_assert(ms_can_fast_dispatch(op->get_req()));
    const MOSDFastDispatchOp *m = static_cast<const MOSDFastDispatchOp*>(
      op->get_req());
    if (m->get_min_epoch() > osdmap->get_epoch()) {
      break;
    }
    session->waiting_on_map.erase(i++);
    op->put();

    spg_t pgid;
    if (m->get_type() == CEPH_MSG_OSD_OP) {
      pg_t actual_pgid = osdmap->raw_pg_to_pg(
	static_cast<const MOSDOp*>(m)->get_pg());
      if (!osdmap->get_primary_shard(actual_pgid, &pgid)) {
	continue;
      }
    } else {
      pgid = m->get_spg();
    }
    enqueue_op(pgid, std::move(op), m->get_map_epoch());
  }

  if (session->waiting_on_map.empty()) {
    clear_session_waiting_on_map(session);
  } else {
    register_session_waiting_on_map(session);
  }
}

void OSD::ms_fast_dispatch(Message *m)
{
  FUNCTRACE(cct);
  if (service.is_stopping()) {
    m->put();
    return;
  }

  // peering event?
  switch (m->get_type()) {
  case CEPH_MSG_PING:
    dout(10) << "ping from " << m->get_source() << dendl;
    m->put();
    return;
  case MSG_MON_COMMAND:
    handle_command(static_cast<MMonCommand*>(m));
    return;
  case MSG_OSD_FORCE_RECOVERY:
    handle_fast_force_recovery(static_cast<MOSDForceRecovery*>(m));
    return;
  case MSG_OSD_SCRUB2:
    handle_fast_scrub(static_cast<MOSDScrub2*>(m));
    return;

  case MSG_OSD_PG_CREATE2:
    return handle_fast_pg_create(static_cast<MOSDPGCreate2*>(m));
  case MSG_OSD_PG_QUERY:
    return handle_fast_pg_query(static_cast<MOSDPGQuery*>(m));
  case MSG_OSD_PG_NOTIFY:
    return handle_fast_pg_notify(static_cast<MOSDPGNotify*>(m));
  case MSG_OSD_PG_INFO:
    return handle_fast_pg_info(static_cast<MOSDPGInfo*>(m));
  case MSG_OSD_PG_REMOVE:
    return handle_fast_pg_remove(static_cast<MOSDPGRemove*>(m));

    // these are single-pg messages that handle themselves
  case MSG_OSD_PG_LOG:
  case MSG_OSD_PG_TRIM:
  case MSG_OSD_BACKFILL_RESERVE:
  case MSG_OSD_RECOVERY_RESERVE:
    {
      MOSDPeeringOp *pm = static_cast<MOSDPeeringOp*>(m);
      if (require_osd_peer(pm)) {
	enqueue_peering_evt(
	  pm->get_spg(),
	  PGPeeringEventRef(pm->get_event()));
      }
      pm->put();
      return;
    }
  }

  OpRequestRef op = op_tracker.create_request<OpRequest, Message*>(m);
  {
#ifdef WITH_LTTNG
    osd_reqid_t reqid = op->get_reqid();
#endif
    tracepoint(osd, ms_fast_dispatch, reqid.name._type,
        reqid.name._num, reqid.tid, reqid.inc);
  }

  if (m->trace)
    op->osd_trace.init("osd op", &trace_endpoint, &m->trace);

  // note sender epoch, min req's epoch
  op->sent_epoch = static_cast<MOSDFastDispatchOp*>(m)->get_map_epoch();
  op->min_epoch = static_cast<MOSDFastDispatchOp*>(m)->get_min_epoch();
  ceph_assert(op->min_epoch <= op->sent_epoch); // sanity check!

  service.maybe_inject_dispatch_delay();

  if (m->get_connection()->has_features(CEPH_FEATUREMASK_RESEND_ON_SPLIT) ||
      m->get_type() != CEPH_MSG_OSD_OP) {
    // queue it directly
    enqueue_op(
      static_cast<MOSDFastDispatchOp*>(m)->get_spg(),
      std::move(op),
      static_cast<MOSDFastDispatchOp*>(m)->get_map_epoch());
  } else {
    // legacy client, and this is an MOSDOp (the *only* fast dispatch
    // message that didn't have an explicit spg_t); we need to map
    // them to an spg_t while preserving delivery order.
    auto priv = m->get_connection()->get_priv();
    if (auto session = static_cast<Session*>(priv.get()); session) {
      std::lock_guard l{session->session_dispatch_lock};
      op->get();
      session->waiting_on_map.push_back(*op);
      OSDMapRef nextmap = service.get_nextmap_reserved();
      dispatch_session_waiting(session, nextmap);
      service.release_map(nextmap);
    }
  }
  OID_EVENT_TRACE_WITH_MSG(m, "MS_FAST_DISPATCH_END", false); 
}

int OSD::ms_handle_authentication(Connection *con)
{
  int ret = 0;
  auto priv = con->get_priv();
  Session *s = static_cast<Session*>(priv.get());
  if (!s) {
    s = new Session(cct, con);
    con->set_priv(RefCountedPtr{s, false});
    s->entity_name = con->get_peer_entity_name();
    dout(10) << __func__ << " new session " << s << " con " << s->con
	     << " entity " << s->entity_name
	     << " addr " << con->get_peer_addrs() << dendl;
  } else {
    dout(10) << __func__ << " existing session " << s << " con " << s->con
	     << " entity " << s->entity_name
	     << " addr " << con->get_peer_addrs() << dendl;
  }

  AuthCapsInfo &caps_info = con->get_peer_caps_info();
  if (caps_info.allow_all) {
    s->caps.set_allow_all();
  } else if (caps_info.caps.length() > 0) {
    bufferlist::const_iterator p = caps_info.caps.cbegin();
    string str;
    try {
      decode(str, p);
    }
    catch (buffer::error& e) {
      dout(10) << __func__ << " session " << s << " " << s->entity_name
	       << " failed to decode caps string" << dendl;
      ret = -EPERM;
    }
    if (!ret) {
      bool success = s->caps.parse(str);
      if (success) {
	dout(10) << __func__ << " session " << s
		 << " " << s->entity_name
		 << " has caps " << s->caps << " '" << str << "'" << dendl;
	ret = 1;
      } else {
	dout(10) << __func__ << " session " << s << " " << s->entity_name
		 << " failed to parse caps '" << str << "'" << dendl;
	ret = -EPERM;
      }
    }
  }
  return ret;
}

void OSD::do_waiters()
{
  ceph_assert(osd_lock.is_locked());

  dout(10) << "do_waiters -- start" << dendl;
  while (!finished.empty()) {
    OpRequestRef next = finished.front();
    finished.pop_front();
    dispatch_op(next);
  }
  dout(10) << "do_waiters -- finish" << dendl;
}

void OSD::dispatch_op(OpRequestRef op)
{
  switch (op->get_req()->get_type()) {

  case MSG_OSD_PG_CREATE:
    handle_pg_create(op);
    break;
  }
}

void OSD::_dispatch(Message *m)
{
  ceph_assert(osd_lock.is_locked());
  dout(20) << "_dispatch " << m << " " << *m << dendl;

  switch (m->get_type()) {
    // -- don't need OSDMap --

    // map and replication
  case CEPH_MSG_OSD_MAP:
    handle_osd_map(static_cast<MOSDMap*>(m));
    break;
  case MSG_MON_GET_PURGED_SNAPS_REPLY:
    handle_get_purged_snaps_reply(static_cast<MMonGetPurgedSnapsReply*>(m));
    break;

    // osd
  case MSG_OSD_SCRUB:
    handle_scrub(static_cast<MOSDScrub*>(m));
    break;

  case MSG_COMMAND:
    handle_command(static_cast<MCommand*>(m));
    return;

    // -- need OSDMap --

  case MSG_OSD_PG_CREATE:
    {
      OpRequestRef op = op_tracker.create_request<OpRequest, Message*>(m);
      if (m->trace)
        op->osd_trace.init("osd op", &trace_endpoint, &m->trace);
      // no map?  starting up?
      if (!osdmap) {
        dout(7) << "no OSDMap, not booted" << dendl;
	logger->inc(l_osd_waiting_for_map);
        waiting_for_osdmap.push_back(op);
	op->mark_delayed("no osdmap");
        break;
      }

      // need OSDMap
      dispatch_op(op);
    }
  }
}

// remove me post-nautilus
void OSD::handle_scrub(MOSDScrub *m)
{
  dout(10) << "handle_scrub " << *m << dendl;
  if (!require_mon_or_mgr_peer(m)) {
    m->put();
    return;
  }
  if (m->fsid != monc->get_fsid()) {
    dout(0) << "handle_scrub fsid " << m->fsid << " != " << monc->get_fsid()
	    << dendl;
    m->put();
    return;
  }

  vector<spg_t> spgs;
  _get_pgids(&spgs);

  if (!m->scrub_pgs.empty()) {
    vector<spg_t> v;
    for (auto pgid : m->scrub_pgs) {
      spg_t pcand;
      if (osdmap->get_primary_shard(pgid, &pcand) &&
	  std::find(spgs.begin(), spgs.end(), pcand) != spgs.end()) {
	v.push_back(pcand);
      }
    }
    spgs.swap(v);
  }

  for (auto pgid : spgs) {
    enqueue_peering_evt(
      pgid,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  get_osdmap_epoch(),
	  get_osdmap_epoch(),
	  PeeringState::RequestScrub(m->deep, m->repair))));
  }

  m->put();
}

void OSD::handle_fast_scrub(MOSDScrub2 *m)
{
  dout(10) << __func__ <<  " " << *m << dendl;
  if (!require_mon_or_mgr_peer(m)) {
    m->put();
    return;
  }
  if (m->fsid != monc->get_fsid()) {
    dout(0) << __func__ << " fsid " << m->fsid << " != " << monc->get_fsid()
	    << dendl;
    m->put();
    return;
  }
  for (auto pgid : m->scrub_pgs) {
    enqueue_peering_evt(
      pgid,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  m->epoch,
	  m->epoch,
	  PeeringState::RequestScrub(m->deep, m->repair))));
  }
  m->put();
}

bool OSD::scrub_random_backoff()
{
  bool coin_flip = (rand() / (double)RAND_MAX >=
		    cct->_conf->osd_scrub_backoff_ratio);
  if (!coin_flip) {
    dout(20) << "scrub_random_backoff lost coin flip, randomly backing off" << dendl;
    return true;
  }
  return false;
}

OSDService::ScrubJob::ScrubJob(CephContext* cct,
			       const spg_t& pg, const utime_t& timestamp,
			       double pool_scrub_min_interval,
			       double pool_scrub_max_interval, bool must)
  : cct(cct),
    pgid(pg),
    sched_time(timestamp),
    deadline(timestamp)
{
  // if not explicitly requested, postpone the scrub with a random delay
  if (!must) {
    double scrub_min_interval = pool_scrub_min_interval > 0 ?
      pool_scrub_min_interval : cct->_conf->osd_scrub_min_interval;
    double scrub_max_interval = pool_scrub_max_interval > 0 ?
      pool_scrub_max_interval : cct->_conf->osd_scrub_max_interval;

    sched_time += scrub_min_interval;
    double r = rand() / (double)RAND_MAX;
    sched_time +=
      scrub_min_interval * cct->_conf->osd_scrub_interval_randomize_ratio * r;
    if (scrub_max_interval == 0) {
      deadline = utime_t();
    } else {
      deadline += scrub_max_interval;
    }

  }
}

bool OSDService::ScrubJob::ScrubJob::operator<(const OSDService::ScrubJob& rhs) const {
  if (sched_time < rhs.sched_time)
    return true;
  if (sched_time > rhs.sched_time)
    return false;
  return pgid < rhs.pgid;
}

bool OSD::scrub_time_permit(utime_t now)
{
  struct tm bdt;
  time_t tt = now.sec();
  localtime_r(&tt, &bdt);

  bool day_permit = false;
  if (cct->_conf->osd_scrub_begin_week_day < cct->_conf->osd_scrub_end_week_day) {
    if (bdt.tm_wday >= cct->_conf->osd_scrub_begin_week_day && bdt.tm_wday < cct->_conf->osd_scrub_end_week_day) {
      day_permit = true;
    }
  } else {
    if (bdt.tm_wday >= cct->_conf->osd_scrub_begin_week_day || bdt.tm_wday < cct->_conf->osd_scrub_end_week_day) {
      day_permit = true;
    }
  }

  if (!day_permit) {
    dout(20) << __func__ << " should run between week day " << cct->_conf->osd_scrub_begin_week_day
            << " - " << cct->_conf->osd_scrub_end_week_day
            << " now " << bdt.tm_wday << " = no" << dendl;
    return false;
  }

  bool time_permit = false;
  if (cct->_conf->osd_scrub_begin_hour < cct->_conf->osd_scrub_end_hour) {
    if (bdt.tm_hour >= cct->_conf->osd_scrub_begin_hour && bdt.tm_hour < cct->_conf->osd_scrub_end_hour) {
      time_permit = true;
    }
  } else {
    if (bdt.tm_hour >= cct->_conf->osd_scrub_begin_hour || bdt.tm_hour < cct->_conf->osd_scrub_end_hour) {
      time_permit = true;
    }
  }
  if (!time_permit) {
    dout(20) << __func__ << " should run between " << cct->_conf->osd_scrub_begin_hour
            << " - " << cct->_conf->osd_scrub_end_hour
            << " now " << bdt.tm_hour << " = no" << dendl;
  } else {
    dout(20) << __func__ << " should run between " << cct->_conf->osd_scrub_begin_hour
            << " - " << cct->_conf->osd_scrub_end_hour
            << " now " << bdt.tm_hour << " = yes" << dendl;
  }
  return time_permit;
}

bool OSD::scrub_load_below_threshold()
{
  double loadavgs[3];
  if (getloadavg(loadavgs, 3) != 3) {
    dout(10) << __func__ << " couldn't read loadavgs\n" << dendl;
    return false;
  }

  // allow scrub if below configured threshold
  long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  double loadavg_per_cpu = cpus > 0 ? loadavgs[0] / cpus : loadavgs[0];
  if (loadavg_per_cpu < cct->_conf->osd_scrub_load_threshold) {
    dout(20) << __func__ << " loadavg per cpu " << loadavg_per_cpu
	     << " < max " << cct->_conf->osd_scrub_load_threshold
	     << " = yes" << dendl;
    return true;
  }

  // allow scrub if below daily avg and currently decreasing
  if (loadavgs[0] < daily_loadavg && loadavgs[0] < loadavgs[2]) {
    dout(20) << __func__ << " loadavg " << loadavgs[0]
	     << " < daily_loadavg " << daily_loadavg
	     << " and < 15m avg " << loadavgs[2]
	     << " = yes" << dendl;
    return true;
  }

  dout(20) << __func__ << " loadavg " << loadavgs[0]
	   << " >= max " << cct->_conf->osd_scrub_load_threshold
	   << " and ( >= daily_loadavg " << daily_loadavg
	   << " or >= 15m avg " << loadavgs[2]
	   << ") = no" << dendl;
  return false;
}

void OSD::sched_scrub()
{
  // if not permitted, fail fast
  if (!service.can_inc_scrubs_pending()) {
    return;
  }
  if (!cct->_conf->osd_scrub_during_recovery && service.is_recovery_active()) {
    dout(20) << __func__ << " not scheduling scrubs due to active recovery" << dendl;
    return;
  }


  utime_t now = ceph_clock_now();
  bool time_permit = scrub_time_permit(now);
  bool load_is_low = scrub_load_below_threshold();
  dout(20) << "sched_scrub load_is_low=" << (int)load_is_low << dendl;

  OSDService::ScrubJob scrub;
  if (service.first_scrub_stamp(&scrub)) {
    do {
      dout(30) << "sched_scrub examine " << scrub.pgid << " at " << scrub.sched_time << dendl;

      if (scrub.sched_time > now) {
	// save ourselves some effort
	dout(10) << "sched_scrub " << scrub.pgid << " scheduled at " << scrub.sched_time
		 << " > " << now << dendl;
	break;
      }

      if ((scrub.deadline.is_zero() || scrub.deadline >= now) && !(time_permit && load_is_low)) {
        dout(10) << __func__ << " not scheduling scrub for " << scrub.pgid << " due to "
                 << (!time_permit ? "time not permit" : "high load") << dendl;
        continue;
      }

      PGRef pg = _lookup_lock_pg(scrub.pgid);
      if (!pg)
	continue;
      // This has already started, so go on to the next scrub job
      if (pg->scrubber.active) {
	pg->unlock();
	dout(30) << __func__ << ": already in progress pgid " << scrub.pgid << dendl;
	continue;
      }
      // If it is reserving, let it resolve before going to the next scrub job
      if (pg->scrubber.reserved) {
	pg->unlock();
	dout(30) << __func__ << ": reserve in progress pgid " << scrub.pgid << dendl;
	break;
      }
      dout(10) << "sched_scrub scrubbing " << scrub.pgid << " at " << scrub.sched_time
	       << (pg->get_must_scrub() ? ", explicitly requested" :
		   (load_is_low ? ", load_is_low" : " deadline < now"))
	       << dendl;
      if (pg->sched_scrub()) {
	pg->unlock();
	break;
      }
      pg->unlock();
    } while (service.next_scrub_stamp(scrub, &scrub));
  }
  dout(20) << "sched_scrub done" << dendl;
}

void OSD::resched_all_scrubs()
{
  dout(10) << __func__ << ": start" << dendl;
  OSDService::ScrubJob scrub;
  if (service.first_scrub_stamp(&scrub)) {
    do {
      dout(20) << __func__ << ": examine " << scrub.pgid << dendl;

      PGRef pg = _lookup_lock_pg(scrub.pgid);
      if (!pg)
	continue;
      if (!pg->scrubber.must_scrub && !pg->scrubber.need_auto) {
        dout(20) << __func__ << ": reschedule " << scrub.pgid << dendl;
        pg->on_info_history_change();
      }
      pg->unlock();
    } while (service.next_scrub_stamp(scrub, &scrub));
  }
  dout(10) << __func__ << ": done" << dendl;
}

MPGStats* OSD::collect_pg_stats()
{
  // This implementation unconditionally sends every is_primary PG's
  // stats every time we're called.  This has equivalent cost to the
  // previous implementation's worst case where all PGs are busy and
  // their stats are always enqueued for sending.
  RWLock::RLocker l(map_lock);

  osd_stat_t cur_stat = service.get_osd_stat();
  cur_stat.os_perf_stat = store->get_cur_stats();

  auto m = new MPGStats(monc->get_fsid(), osdmap->get_epoch());
  m->osd_stat = cur_stat;

  std::lock_guard lec{min_last_epoch_clean_lock};
  min_last_epoch_clean = osdmap->get_epoch();
  min_last_epoch_clean_pgs.clear();

  std::set<int64_t> pool_set;
  vector<PGRef> pgs;
  _get_pgs(&pgs);
  for (auto& pg : pgs) {
    auto pool = pg->pg_id.pgid.pool();
    pool_set.emplace((int64_t)pool);
    if (!pg->is_primary()) {
      continue;
    }
    pg->get_pg_stats([&](const pg_stat_t& s, epoch_t lec) {
	m->pg_stat[pg->pg_id.pgid] = s;
	min_last_epoch_clean = min(min_last_epoch_clean, lec);
	min_last_epoch_clean_pgs.push_back(pg->pg_id.pgid);
      });
  }
  store_statfs_t st;
  bool per_pool_stats = false;
  for (auto p : pool_set) {
    int r = store->pool_statfs(p, &st);
    if (r == -ENOTSUP) {
      break;
    } else {
      assert(r >= 0);
      m->pool_stat[p] = st;
      per_pool_stats = true;
    }
  }

  // indicate whether we are reporting per-pool stats
  m->osd_stat.num_osds = 1;
  m->osd_stat.num_per_pool_osds = per_pool_stats ? 1 : 0;

  return m;
}

vector<DaemonHealthMetric> OSD::get_health_metrics()
{
  vector<DaemonHealthMetric> metrics;
  {
    utime_t oldest_secs;
    const utime_t now = ceph_clock_now();
    auto too_old = now;
    too_old -= cct->_conf.get_val<double>("osd_op_complaint_time");
    int slow = 0;
    TrackedOpRef oldest_op;
    auto count_slow_ops = [&](TrackedOp& op) {
      if (op.get_initiated() < too_old) {
	lgeneric_subdout(cct,osd,20) << "slow op " << op.get_desc()
	                             << " initiated "
	                             << op.get_initiated() << dendl;
	slow++;
	if (!oldest_op || op.get_initiated() < oldest_op->get_initiated()) {
	  oldest_op = &op;
	}
	return true;
      } else {
	return false;
      }
    };
    if (op_tracker.visit_ops_in_flight(&oldest_secs, count_slow_ops)) {
      if (slow) {
	derr << __func__ << " reporting " << slow << " slow ops, oldest is "
	     << oldest_op->get_desc() << dendl;
      }
      metrics.emplace_back(daemon_metric::SLOW_OPS, slow, oldest_secs);
    } else {
      // no news is not good news.
      metrics.emplace_back(daemon_metric::SLOW_OPS, 0, 0);
    }
  }
  {
    std::lock_guard l(pending_creates_lock);
    auto n_primaries = pending_creates_from_mon;
    for (const auto& create : pending_creates_from_osd) {
      if (create.second) {
	n_primaries++;
      }
    }
    metrics.emplace_back(daemon_metric::PENDING_CREATING_PGS, n_primaries);
  }
  return metrics;
}

// =====================================================
// MAP

void OSD::wait_for_new_map(OpRequestRef op)
{
  // ask?
  if (waiting_for_osdmap.empty()) {
    osdmap_subscribe(osdmap->get_epoch() + 1, false);
  }

  logger->inc(l_osd_waiting_for_map);
  waiting_for_osdmap.push_back(op);
  op->mark_delayed("wait for new map");
}


/** update_map
 * assimilate new OSDMap(s).  scan pgs, etc.
 */

void OSD::note_down_osd(int peer)
{
  ceph_assert(osd_lock.is_locked());
  cluster_messenger->mark_down_addrs(osdmap->get_cluster_addrs(peer));

  heartbeat_lock.Lock();
  failure_queue.erase(peer);
  failure_pending.erase(peer);
  map<int,HeartbeatInfo>::iterator p = heartbeat_peers.find(peer);
  if (p != heartbeat_peers.end()) {
    p->second.con_back->mark_down();
    if (p->second.con_front) {
      p->second.con_front->mark_down();
    }
    heartbeat_peers.erase(p);
  }
  heartbeat_lock.Unlock();
}

void OSD::note_up_osd(int peer)
{
  heartbeat_set_peers_need_update();
}

struct C_OnMapCommit : public Context {
  OSD *osd;
  epoch_t first, last;
  MOSDMap *msg;
  C_OnMapCommit(OSD *o, epoch_t f, epoch_t l, MOSDMap *m)
    : osd(o), first(f), last(l), msg(m) {}
  void finish(int r) override {
    osd->_committed_osd_maps(first, last, msg);
    msg->put();
  }
};

void OSD::osdmap_subscribe(version_t epoch, bool force_request)
{
  std::lock_guard l(osdmap_subscribe_lock);
  if (latest_subscribed_epoch >= epoch && !force_request)
    return;

  latest_subscribed_epoch = std::max<uint64_t>(epoch, latest_subscribed_epoch);

  if (monc->sub_want_increment("osdmap", epoch, CEPH_SUBSCRIBE_ONETIME) ||
      force_request) {
    monc->renew_subs();
  }
}

void OSD::trim_maps(epoch_t oldest, int nreceived, bool skip_maps)
{
  epoch_t min = std::min(oldest, service.map_cache.cached_key_lower_bound());
  if (min <= superblock.oldest_map)
    return;

  int num = 0;
  ObjectStore::Transaction t;
  for (epoch_t e = superblock.oldest_map; e < min; ++e) {
    dout(20) << " removing old osdmap epoch " << e << dendl;
    t.remove(coll_t::meta(), get_osdmap_pobject_name(e));
    t.remove(coll_t::meta(), get_inc_osdmap_pobject_name(e));
    superblock.oldest_map = e + 1;
    num++;
    if (num >= cct->_conf->osd_target_transaction_size && num >= nreceived) {
      service.publish_superblock(superblock);
      write_superblock(t);
      int tr = store->queue_transaction(service.meta_ch, std::move(t), nullptr);
      ceph_assert(tr == 0);
      num = 0;
      if (!skip_maps) {
	// skip_maps leaves us with a range of old maps if we fail to remove all
	// of them before moving superblock.oldest_map forward to the first map
	// in the incoming MOSDMap msg. so we should continue removing them in
	// this case, even we could do huge series of delete transactions all at
	// once.
	break;
      }
    }
  }
  if (num > 0) {
    service.publish_superblock(superblock);
    write_superblock(t);
    int tr = store->queue_transaction(service.meta_ch, std::move(t), nullptr);
    ceph_assert(tr == 0);
  }
  // we should not remove the cached maps
  ceph_assert(min <= service.map_cache.cached_key_lower_bound());
}

void OSD::handle_osd_map(MOSDMap *m)
{
  // wait for pgs to catch up
  {
    // we extend the map cache pins to accomodate pgs slow to consume maps
    // for some period, until we hit the max_lag_factor bound, at which point
    // we block here to stop injesting more maps than they are able to keep
    // up with.
    epoch_t max_lag = cct->_conf->osd_map_cache_size *
      m_osd_pg_epoch_max_lag_factor;
    ceph_assert(max_lag > 0);
    epoch_t osd_min = get_min_pg_epoch();
    if (osd_min > 0 &&
	osdmap->get_epoch() > max_lag &&
	osdmap->get_epoch() - max_lag > osd_min) {
      epoch_t need = osdmap->get_epoch() - max_lag;
      dout(10) << __func__ << " waiting for pgs to catch up (need " << need
	       << " max_lag " << max_lag << ")" << dendl;
      for (auto shard : shards) {
	epoch_t min = shard->get_min_pg_epoch();
	if (need > min) {
	  dout(10) << __func__ << " waiting for pgs to consume " << need
		   << " (shard " << shard->shard_id << " min " << min
		   << ", map cache is " << cct->_conf->osd_map_cache_size
		   << ", max_lag_factor " << m_osd_pg_epoch_max_lag_factor
		   << ")" << dendl;
	  unlock_guard unlock{osd_lock};
	  shard->wait_min_pg_epoch(need);
	}
      }
    }
  }

  ceph_assert(osd_lock.is_locked());
  map<epoch_t,OSDMapRef> added_maps;
  map<epoch_t,bufferlist> added_maps_bl;
  if (m->fsid != monc->get_fsid()) {
    dout(0) << "handle_osd_map fsid " << m->fsid << " != "
	    << monc->get_fsid() << dendl;
    m->put();
    return;
  }
  if (is_initializing()) {
    dout(0) << "ignoring osdmap until we have initialized" << dendl;
    m->put();
    return;
  }

  auto priv = m->get_connection()->get_priv();
  if (auto session = static_cast<Session *>(priv.get());
      session && !(session->entity_name.is_mon() ||
		   session->entity_name.is_osd())) {
    //not enough perms!
    dout(10) << "got osd map from Session " << session
             << " which we can't take maps from (not a mon or osd)" << dendl;
    m->put();
    return;
  }

  // share with the objecter
  if (!is_preboot())
    service.objecter->handle_osd_map(m);

  epoch_t first = m->get_first();
  epoch_t last = m->get_last();
  dout(3) << "handle_osd_map epochs [" << first << "," << last << "], i have "
	  << superblock.newest_map
	  << ", src has [" << m->oldest_map << "," << m->newest_map << "]"
	  << dendl;

  logger->inc(l_osd_map);
  logger->inc(l_osd_mape, last - first + 1);
  if (first <= superblock.newest_map)
    logger->inc(l_osd_mape_dup, superblock.newest_map - first + 1);
  if (service.max_oldest_map < m->oldest_map) {
    service.max_oldest_map = m->oldest_map;
    ceph_assert(service.max_oldest_map >= superblock.oldest_map);
  }

  // make sure there is something new, here, before we bother flushing
  // the queues and such
  if (last <= superblock.newest_map) {
    dout(10) << " no new maps here, dropping" << dendl;
    m->put();
    return;
  }

  // missing some?
  bool skip_maps = false;
  if (first > superblock.newest_map + 1) {
    dout(10) << "handle_osd_map message skips epochs "
	     << superblock.newest_map + 1 << ".." << (first-1) << dendl;
    if (m->oldest_map <= superblock.newest_map + 1) {
      osdmap_subscribe(superblock.newest_map + 1, false);
      m->put();
      return;
    }
    // always try to get the full range of maps--as many as we can.  this
    //  1- is good to have
    //  2- is at present the only way to ensure that we get a *full* map as
    //     the first map!
    if (m->oldest_map < first) {
      osdmap_subscribe(m->oldest_map - 1, true);
      m->put();
      return;
    }
    skip_maps = true;
  }

  ObjectStore::Transaction t;
  uint64_t txn_size = 0;

  map<epoch_t,mempool::osdmap::map<int64_t,snap_interval_set_t>> purged_snaps;

  // store new maps: queue for disk and put in the osdmap cache
  epoch_t start = std::max(superblock.newest_map + 1, first);
  for (epoch_t e = start; e <= last; e++) {
    if (txn_size >= t.get_num_bytes()) {
      derr << __func__ << " transaction size overflowed" << dendl;
      ceph_assert(txn_size < t.get_num_bytes());
    }
    txn_size = t.get_num_bytes();
    map<epoch_t,bufferlist>::iterator p;
    p = m->maps.find(e);
    if (p != m->maps.end()) {
      dout(10) << "handle_osd_map  got full map for epoch " << e << dendl;
      OSDMap *o = new OSDMap;
      bufferlist& bl = p->second;

      o->decode(bl);

      purged_snaps[e] = o->get_new_purged_snaps();

      ghobject_t fulloid = get_osdmap_pobject_name(e);
      t.write(coll_t::meta(), fulloid, 0, bl.length(), bl);
      added_maps[e] = add_map(o);
      added_maps_bl[e] = bl;
      got_full_map(e);
      continue;
    }

    p = m->incremental_maps.find(e);
    if (p != m->incremental_maps.end()) {
      dout(10) << "handle_osd_map  got inc map for epoch " << e << dendl;
      bufferlist& bl = p->second;
      ghobject_t oid = get_inc_osdmap_pobject_name(e);
      t.write(coll_t::meta(), oid, 0, bl.length(), bl);

      OSDMap *o = new OSDMap;
      if (e > 1) {
	bufferlist obl;
        bool got = get_map_bl(e - 1, obl);
	if (!got) {
	  auto p = added_maps_bl.find(e - 1);
	  ceph_assert(p != added_maps_bl.end());
	  obl = p->second;
	}
	o->decode(obl);
      }

      OSDMap::Incremental inc;
      auto p = bl.cbegin();
      inc.decode(p);

      if (o->apply_incremental(inc) < 0) {
	derr << "ERROR: bad fsid?  i have " << osdmap->get_fsid() << " and inc has " << inc.fsid << dendl;
	ceph_abort_msg("bad fsid");
      }

      bufferlist fbl;
      o->encode(fbl, inc.encode_features | CEPH_FEATURE_RESERVED);

      bool injected_failure = false;
      if (cct->_conf->osd_inject_bad_map_crc_probability > 0 &&
	  (rand() % 10000) < cct->_conf->osd_inject_bad_map_crc_probability*10000.0) {
	derr << __func__ << " injecting map crc failure" << dendl;
	injected_failure = true;
      }

      if ((inc.have_crc && o->get_crc() != inc.full_crc) || injected_failure) {
	dout(2) << "got incremental " << e
		<< " but failed to encode full with correct crc; requesting"
		<< dendl;
	clog->warn() << "failed to encode map e" << e << " with expected crc";
	dout(20) << "my encoded map was:\n";
	fbl.hexdump(*_dout);
	*_dout << dendl;
	delete o;
	request_full_map(e, last);
	last = e - 1;
	break;
      }
      got_full_map(e);
      purged_snaps[e] = o->get_new_purged_snaps();

      ghobject_t fulloid = get_osdmap_pobject_name(e);
      t.write(coll_t::meta(), fulloid, 0, fbl.length(), fbl);
      added_maps[e] = add_map(o);
      added_maps_bl[e] = fbl;
      continue;
    }

    ceph_abort_msg("MOSDMap lied about what maps it had?");
  }

  // even if this map isn't from a mon, we may have satisfied our subscription
  monc->sub_got("osdmap", last);

  if (!m->maps.empty() && requested_full_first) {
    dout(10) << __func__ << " still missing full maps " << requested_full_first
	     << ".." << requested_full_last << dendl;
    rerequest_full_maps();
  }

  if (superblock.oldest_map) {
    // make sure we at least keep pace with incoming maps
    trim_maps(m->oldest_map, last - first + 1, skip_maps);
    pg_num_history.prune(superblock.oldest_map);
  }

  if (!superblock.oldest_map || skip_maps)
    superblock.oldest_map = first;
  superblock.newest_map = last;
  superblock.current_epoch = last;

  // note in the superblock that we were clean thru the prior epoch
  epoch_t boot_epoch = service.get_boot_epoch();
  if (boot_epoch && boot_epoch >= superblock.mounted) {
    superblock.mounted = boot_epoch;
    superblock.clean_thru = last;
  }

  // check for pg_num changes and deleted pools
  OSDMapRef lastmap;
  for (auto& i : added_maps) {
    if (!lastmap) {
      if (!(lastmap = service.try_get_map(i.first - 1))) {
        dout(10) << __func__ << " can't get previous map " << i.first - 1
                 << " probably first start of this osd" << dendl;
        continue;
      }
    }
    ceph_assert(lastmap->get_epoch() + 1 == i.second->get_epoch());
    for (auto& j : lastmap->get_pools()) {
      if (!i.second->have_pg_pool(j.first)) {
	pg_num_history.log_pool_delete(i.first, j.first);
	dout(10) << __func__ << " recording final pg_pool_t for pool "
		 << j.first << dendl;
	// this information is needed by _make_pg() if have to restart before
	// the pool is deleted and need to instantiate a new (zombie) PG[Pool].
	ghobject_t obj = make_final_pool_info_oid(j.first);
	bufferlist bl;
	encode(j.second, bl, CEPH_FEATURES_ALL);
	string name = lastmap->get_pool_name(j.first);
	encode(name, bl);
	map<string,string> profile;
	if (lastmap->get_pg_pool(j.first)->is_erasure()) {
	  profile = lastmap->get_erasure_code_profile(
	    lastmap->get_pg_pool(j.first)->erasure_code_profile);
	}
	encode(profile, bl);
	t.write(coll_t::meta(), obj, 0, bl.length(), bl);
	service.store_deleted_pool_pg_num(j.first, j.second.get_pg_num());
      } else if (unsigned new_pg_num = i.second->get_pg_num(j.first);
		 new_pg_num != j.second.get_pg_num()) {
	dout(10) << __func__ << " recording pool " << j.first << " pg_num "
		 << j.second.get_pg_num() << " -> " << new_pg_num << dendl;
	pg_num_history.log_pg_num_change(i.first, j.first, new_pg_num);
      }
    }
    for (auto& j : i.second->get_pools()) {
      if (!lastmap->have_pg_pool(j.first)) {
	dout(10) << __func__ << " recording new pool " << j.first << " pg_num "
		 << j.second.get_pg_num() << dendl;
	pg_num_history.log_pg_num_change(i.first, j.first,
					 j.second.get_pg_num());
      }
    }
    lastmap = i.second;
  }
  pg_num_history.epoch = last;
  {
    bufferlist bl;
    ::encode(pg_num_history, bl);
    t.write(coll_t::meta(), make_pg_num_history_oid(), 0, bl.length(), bl);
    dout(20) << __func__ << " pg_num_history " << pg_num_history << dendl;
  }

  // record new purged_snaps
  if (superblock.purged_snaps_last == start - 1) {
    SnapMapper::record_purged_snaps(cct, store, service.meta_ch,
				    make_purged_snaps_oid(), &t,
				    purged_snaps);
    superblock.purged_snaps_last = last;
  } else {
    dout(10) << __func__ << " superblock purged_snaps_last is "
	     << superblock.purged_snaps_last
	     << ", not recording new purged_snaps" << dendl;
  }

  // superblock and commit
  write_superblock(t);
  t.register_on_commit(new C_OnMapCommit(this, start, last, m));
  store->queue_transaction(
    service.meta_ch,
    std::move(t));
  service.publish_superblock(superblock);
}

void OSD::_committed_osd_maps(epoch_t first, epoch_t last, MOSDMap *m)
{
  dout(10) << __func__ << " " << first << ".." << last << dendl;
  if (is_stopping()) {
    dout(10) << __func__ << " bailing, we are shutting down" << dendl;
    return;
  }
  std::lock_guard l(osd_lock);
  if (is_stopping()) {
    dout(10) << __func__ << " bailing, we are shutting down" << dendl;
    return;
  }
  map_lock.get_write();

  bool do_shutdown = false;
  bool do_restart = false;
  bool network_error = false;

  // advance through the new maps
  for (epoch_t cur = first; cur <= last; cur++) {
    dout(10) << " advance to epoch " << cur
	     << " (<= last " << last
	     << " <= newest_map " << superblock.newest_map
	     << ")" << dendl;

    OSDMapRef newmap = get_map(cur);
    ceph_assert(newmap);  // we just cached it above!

    // start blacklisting messages sent to peers that go down.
    service.pre_publish_map(newmap);

    // kill connections to newly down osds
    bool waited_for_reservations = false;
    set<int> old;
    osdmap->get_all_osds(old);
    for (set<int>::iterator p = old.begin(); p != old.end(); ++p) {
      if (*p != whoami &&
	  osdmap->is_up(*p) && // in old map
	  newmap->is_down(*p)) {    // but not the new one
        if (!waited_for_reservations) {
          service.await_reserved_maps();
          waited_for_reservations = true;
        }
	note_down_osd(*p);
      } else if (*p != whoami &&
                osdmap->is_down(*p) &&
                newmap->is_up(*p)) {
        note_up_osd(*p);
      }
    }

    if (osdmap->is_noup(whoami) != newmap->is_noup(whoami)) {
      dout(10) << __func__ << " NOUP flag changed in " << newmap->get_epoch()
	       << dendl;
      if (is_booting()) {
	// this captures the case where we sent the boot message while
	// NOUP was being set on the mon and our boot request was
	// dropped, and then later it is cleared.  it imperfectly
	// handles the case where our original boot message was not
	// dropped and we restart even though we might have booted, but
	// that is harmless (boot will just take slightly longer).
	do_restart = true;
      }
    }

    osdmap = newmap;
    epoch_t up_epoch;
    epoch_t boot_epoch;
    service.retrieve_epochs(&boot_epoch, &up_epoch, NULL);
    if (!up_epoch &&
	osdmap->is_up(whoami) &&
	osdmap->get_addrs(whoami) == client_messenger->get_myaddrs()) {
      up_epoch = osdmap->get_epoch();
      dout(10) << "up_epoch is " << up_epoch << dendl;
      if (!boot_epoch) {
	boot_epoch = osdmap->get_epoch();
	dout(10) << "boot_epoch is " << boot_epoch << dendl;
      }
      service.set_epochs(&boot_epoch, &up_epoch, NULL);
    }
  }

  epoch_t _bind_epoch = service.get_bind_epoch();
  if (osdmap->is_up(whoami) &&
      osdmap->get_addrs(whoami).legacy_equals(
	client_messenger->get_myaddrs()) &&
      _bind_epoch < osdmap->get_up_from(whoami)) {

    if (is_booting()) {
      dout(1) << "state: booting -> active" << dendl;
      set_state(STATE_ACTIVE);
      do_restart = false;

      // set incarnation so that osd_reqid_t's we generate for our
      // objecter requests are unique across restarts.
      service.objecter->set_client_incarnation(osdmap->get_epoch());
      cancel_pending_failures();
    }
  }

  if (osdmap->get_epoch() > 0 &&
      is_active()) {
    if (!osdmap->exists(whoami)) {
      dout(0) << "map says i do not exist.  shutting down." << dendl;
      do_shutdown = true;   // don't call shutdown() while we have
			    // everything paused
    } else if (osdmap->is_stop(whoami)) {
      dout(0) << "map says i am stopped by admin. shutting down." << dendl;
      do_shutdown = true;
    } else if (!osdmap->is_up(whoami) ||
	       !osdmap->get_addrs(whoami).legacy_equals(
		 client_messenger->get_myaddrs()) ||
	       !osdmap->get_cluster_addrs(whoami).legacy_equals(
		 cluster_messenger->get_myaddrs()) ||
	       !osdmap->get_hb_back_addrs(whoami).legacy_equals(
		 hb_back_server_messenger->get_myaddrs()) ||
	       !osdmap->get_hb_front_addrs(whoami).legacy_equals(
		 hb_front_server_messenger->get_myaddrs())) {
      if (!osdmap->is_up(whoami)) {
	if (service.is_preparing_to_stop() || service.is_stopping()) {
	  service.got_stop_ack();
	} else {
          clog->warn() << "Monitor daemon marked osd." << whoami << " down, "
                          "but it is still running";
          clog->debug() << "map e" << osdmap->get_epoch()
                        << " wrongly marked me down at e"
                        << osdmap->get_down_at(whoami);
	}
      } else if (!osdmap->get_addrs(whoami).legacy_equals(
		   client_messenger->get_myaddrs())) {
	clog->error() << "map e" << osdmap->get_epoch()
		      << " had wrong client addr (" << osdmap->get_addrs(whoami)
		      << " != my " << client_messenger->get_myaddrs() << ")";
      } else if (!osdmap->get_cluster_addrs(whoami).legacy_equals(
		   cluster_messenger->get_myaddrs())) {
	clog->error() << "map e" << osdmap->get_epoch()
		      << " had wrong cluster addr ("
		      << osdmap->get_cluster_addrs(whoami)
		      << " != my " << cluster_messenger->get_myaddrs() << ")";
      } else if (!osdmap->get_hb_back_addrs(whoami).legacy_equals(
		   hb_back_server_messenger->get_myaddrs())) {
	clog->error() << "map e" << osdmap->get_epoch()
		      << " had wrong heartbeat back addr ("
		      << osdmap->get_hb_back_addrs(whoami)
		      << " != my " << hb_back_server_messenger->get_myaddrs()
		      << ")";
      } else if (!osdmap->get_hb_front_addrs(whoami).legacy_equals(
		   hb_front_server_messenger->get_myaddrs())) {
	clog->error() << "map e" << osdmap->get_epoch()
		      << " had wrong heartbeat front addr ("
		      << osdmap->get_hb_front_addrs(whoami)
		      << " != my " << hb_front_server_messenger->get_myaddrs()
		      << ")";
      }

      if (!service.is_stopping()) {
        epoch_t up_epoch = 0;
        epoch_t bind_epoch = osdmap->get_epoch();
        service.set_epochs(NULL,&up_epoch, &bind_epoch);
	do_restart = true;

	//add markdown log
	utime_t now = ceph_clock_now();
	utime_t grace = utime_t(cct->_conf->osd_max_markdown_period, 0);
	osd_markdown_log.push_back(now);
	//clear all out-of-date log
	while (!osd_markdown_log.empty() &&
	       osd_markdown_log.front() + grace < now)
	  osd_markdown_log.pop_front();
	if ((int)osd_markdown_log.size() > cct->_conf->osd_max_markdown_count) {
	  dout(0) << __func__ << " marked down "
		  << osd_markdown_log.size()
		  << " > osd_max_markdown_count "
		  << cct->_conf->osd_max_markdown_count
		  << " in last " << grace << " seconds, shutting down"
		  << dendl;
	  do_restart = false;
	  do_shutdown = true;
	}

	start_waiting_for_healthy();

	set<int> avoid_ports;
#if defined(__FreeBSD__)
        // prevent FreeBSD from grabbing the client_messenger port during
        // rebinding. In which case a cluster_meesneger will connect also 
	// to the same port
	client_messenger->get_myaddrs().get_ports(&avoid_ports);
#endif
	cluster_messenger->get_myaddrs().get_ports(&avoid_ports);

	int r = cluster_messenger->rebind(avoid_ports);
	if (r != 0) {
	  do_shutdown = true;  // FIXME: do_restart?
          network_error = true;
          dout(0) << __func__ << " marked down:"
                  << " rebind cluster_messenger failed" << dendl;
        }

	hb_back_server_messenger->mark_down_all();
	hb_front_server_messenger->mark_down_all();
	hb_front_client_messenger->mark_down_all();
	hb_back_client_messenger->mark_down_all();

	reset_heartbeat_peers(true);
      }
    }
  }

  map_lock.put_write();

  check_osdmap_features();

  // yay!
  consume_map();

  if (is_active() || is_waiting_for_healthy())
    maybe_update_heartbeat_peers();

  if (is_active()) {
    activate_map();
  }

  if (do_shutdown) {
    if (network_error) {
      cancel_pending_failures();
    }
    // trigger shutdown in a different thread
    dout(0) << __func__ << " shutdown OSD via async signal" << dendl;
    queue_async_signal(SIGINT);
  }
  else if (m->newest_map && m->newest_map > last) {
    dout(10) << " msg say newest map is " << m->newest_map
	     << ", requesting more" << dendl;
    osdmap_subscribe(osdmap->get_epoch()+1, false);
  }
  else if (is_preboot()) {
    if (m->get_source().is_mon())
      _preboot(m->oldest_map, m->newest_map);
    else
      start_boot();
  }
  else if (do_restart)
    start_boot();

}

void OSD::check_osdmap_features()
{
  // adjust required feature bits?

  // we have to be a bit careful here, because we are accessing the
  // Policy structures without taking any lock.  in particular, only
  // modify integer values that can safely be read by a racing CPU.
  // since we are only accessing existing Policy structures a their
  // current memory location, and setting or clearing bits in integer
  // fields, and we are the only writer, this is not a problem.

  {
    Messenger::Policy p = client_messenger->get_default_policy();
    uint64_t mask;
    uint64_t features = osdmap->get_features(entity_name_t::TYPE_CLIENT, &mask);
    if ((p.features_required & mask) != features) {
      dout(0) << "crush map has features " << features
	      << ", adjusting msgr requires for clients" << dendl;
      p.features_required = (p.features_required & ~mask) | features;
      client_messenger->set_default_policy(p);
    }
  }
  {
    Messenger::Policy p = client_messenger->get_policy(entity_name_t::TYPE_MON);
    uint64_t mask;
    uint64_t features = osdmap->get_features(entity_name_t::TYPE_MON, &mask);
    if ((p.features_required & mask) != features) {
      dout(0) << "crush map has features " << features
	      << " was " << p.features_required
	      << ", adjusting msgr requires for mons" << dendl;
      p.features_required = (p.features_required & ~mask) | features;
      client_messenger->set_policy(entity_name_t::TYPE_MON, p);
    }
  }
  {
    Messenger::Policy p = cluster_messenger->get_policy(entity_name_t::TYPE_OSD);
    uint64_t mask;
    uint64_t features = osdmap->get_features(entity_name_t::TYPE_OSD, &mask);

    if ((p.features_required & mask) != features) {
      dout(0) << "crush map has features " << features
	      << ", adjusting msgr requires for osds" << dendl;
      p.features_required = (p.features_required & ~mask) | features;
      cluster_messenger->set_policy(entity_name_t::TYPE_OSD, p);
    }

    if (!superblock.compat_features.incompat.contains(CEPH_OSD_FEATURE_INCOMPAT_SHARDS)) {
      dout(0) << __func__ << " enabling on-disk ERASURE CODES compat feature" << dendl;
      superblock.compat_features.incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_SHARDS);
      ObjectStore::Transaction t;
      write_superblock(t);
      int err = store->queue_transaction(service.meta_ch, std::move(t), NULL);
      ceph_assert(err == 0);
    }
  }

  if (osdmap->require_osd_release < ceph_release_t::nautilus) {
    hb_front_server_messenger->set_require_authorizer(false);
    hb_back_server_messenger->set_require_authorizer(false);
  } else {
    hb_front_server_messenger->set_require_authorizer(true);
    hb_back_server_messenger->set_require_authorizer(true);
  }

  if (osdmap->require_osd_release != last_require_osd_release) {
    dout(1) << __func__ << " require_osd_release " << last_require_osd_release
	    << " -> " << to_string(osdmap->require_osd_release) << dendl;
    store->write_meta("require_osd_release",
		      stringify((int)osdmap->require_osd_release));
    last_require_osd_release = osdmap->require_osd_release;
  }
}

struct C_FinishSplits : public Context {
  OSD *osd;
  set<PGRef> pgs;
  C_FinishSplits(OSD *osd, const set<PGRef> &in)
    : osd(osd), pgs(in) {}
  void finish(int r) override {
    osd->_finish_splits(pgs);
  }
};

void OSD::_finish_splits(set<PGRef>& pgs)
{
  dout(10) << __func__ << " " << pgs << dendl;
  if (is_stopping())
    return;
  PeeringCtx rctx = create_context();
  for (set<PGRef>::iterator i = pgs.begin();
       i != pgs.end();
       ++i) {
    PG *pg = i->get();

    pg->lock();
    dout(10) << __func__ << " " << *pg << dendl;
    epoch_t e = pg->get_osdmap_epoch();
    pg->handle_initialize(rctx);
    pg->queue_null(e, e);
    dispatch_context_transaction(rctx, pg);
    pg->unlock();

    unsigned shard_index = pg->pg_id.hash_to_shard(num_shards);
    shards[shard_index]->register_and_wake_split_child(pg);
  }

  dispatch_context(rctx, 0, service.get_osdmap());
};

epoch_t OSD::get_min_pg_epoch()
{
  epoch_t osd_min = 0;
  for (auto shard : shards) {
    epoch_t min = shard->get_min_pg_epoch();
    if (osd_min == 0 || min < osd_min) {
      osd_min = min;
    }
  }
  return osd_min;
}

bool OSD::add_merge_waiter(OSDMapRef nextmap, spg_t target, PGRef src,
			   unsigned need)
{
  std::lock_guard l(merge_lock);
  auto& p = merge_waiters[nextmap->get_epoch()][target];
  p[src->pg_id] = src;
  dout(10) << __func__ << " added merge_waiter " << src->pg_id
	   << " for " << target  << ", have " << p.size() << "/" << need
	   << dendl;
  return p.size() == need;
}

bool OSD::advance_pg(
  epoch_t osd_epoch,
  PG *pg,
  ThreadPool::TPHandle &handle,
  PeeringCtx &rctx)
{
  if (osd_epoch <= pg->get_osdmap_epoch()) {
    return true;
  }
  ceph_assert(pg->is_locked());
  OSDMapRef lastmap = pg->get_osdmap();
  ceph_assert(lastmap->get_epoch() < osd_epoch);
  set<PGRef> new_pgs;  // any split children
  bool ret = true;

  unsigned old_pg_num = lastmap->have_pg_pool(pg->pg_id.pool()) ?
    lastmap->get_pg_num(pg->pg_id.pool()) : 0;
  for (epoch_t next_epoch = pg->get_osdmap_epoch() + 1;
       next_epoch <= osd_epoch;
       ++next_epoch) {
    OSDMapRef nextmap = service.try_get_map(next_epoch);
    if (!nextmap) {
      dout(20) << __func__ << " missing map " << next_epoch << dendl;
      continue;
    }

    unsigned new_pg_num =
      (old_pg_num && nextmap->have_pg_pool(pg->pg_id.pool())) ?
      nextmap->get_pg_num(pg->pg_id.pool()) : 0;
    if (old_pg_num && new_pg_num && old_pg_num != new_pg_num) {
      // check for merge
      if (nextmap->have_pg_pool(pg->pg_id.pool())) {
	spg_t parent;
	if (pg->pg_id.is_merge_source(
	      old_pg_num,
	      new_pg_num,
	      &parent)) {
	  // we are merge source
	  PGRef spg = pg; // carry a ref
	  dout(1) << __func__ << " " << pg->pg_id
		  << " is merge source, target is " << parent
		   << dendl;
	  pg->write_if_dirty(rctx);
	  dispatch_context_transaction(rctx, pg, &handle);
	  pg->ch->flush();
	  pg->on_shutdown();
	  OSDShard *sdata = pg->osd_shard;
	  {
	    std::lock_guard l(sdata->shard_lock);
	    if (pg->pg_slot) {
	      sdata->_detach_pg(pg->pg_slot);
	      // update pg count now since we might not get an osdmap
	      // any time soon.
	      if (pg->is_primary())
		logger->dec(l_osd_pg_primary);
	      else if (pg->is_replica())
		logger->dec(l_osd_pg_replica);
	      else
		logger->dec(l_osd_pg_stray);
	    }
	  }
	  pg->unlock();

	  set<spg_t> children;
	  parent.is_split(new_pg_num, old_pg_num, &children);
	  if (add_merge_waiter(nextmap, parent, pg, children.size())) {
	    enqueue_peering_evt(
	      parent,
	      PGPeeringEventRef(
		std::make_shared<PGPeeringEvent>(
		  nextmap->get_epoch(),
		  nextmap->get_epoch(),
		  NullEvt())));
	  }
	  ret = false;
	  goto out;
	} else if (pg->pg_id.is_merge_target(old_pg_num, new_pg_num)) {
	  // we are merge target
	  set<spg_t> children;
	  pg->pg_id.is_split(new_pg_num, old_pg_num, &children);
	  dout(20) << __func__ << " " << pg->pg_id
		   << " is merge target, sources are " << children
		   << dendl;
	  map<spg_t,PGRef> sources;
	  {
	    std::lock_guard l(merge_lock);
	    auto& s = merge_waiters[nextmap->get_epoch()][pg->pg_id];
	    unsigned need = children.size();
	    dout(20) << __func__ << " have " << s.size() << "/"
		     << need << dendl;
	    if (s.size() == need) {
	      sources.swap(s);
	      merge_waiters[nextmap->get_epoch()].erase(pg->pg_id);
	      if (merge_waiters[nextmap->get_epoch()].empty()) {
		merge_waiters.erase(nextmap->get_epoch());
	      }
	    }
	  }
	  if (!sources.empty()) {
	    unsigned new_pg_num = nextmap->get_pg_num(pg->pg_id.pool());
	    unsigned split_bits = pg->pg_id.get_split_bits(new_pg_num);
	    dout(1) << __func__ << " merging " << pg->pg_id << dendl;
	    pg->merge_from(
	      sources, rctx, split_bits,
	      nextmap->get_pg_pool(
		pg->pg_id.pool())->last_pg_merge_meta);
	    pg->pg_slot->waiting_for_merge_epoch = 0;
	  } else {
	    dout(20) << __func__ << " not ready to merge yet" << dendl;
	    pg->write_if_dirty(rctx);
	    pg->unlock();
	    // kick source(s) to get them ready
	    for (auto& i : children) {
	      dout(20) << __func__ << " kicking source " << i << dendl;
	      enqueue_peering_evt(
		i,
		PGPeeringEventRef(
		  std::make_shared<PGPeeringEvent>(
		    nextmap->get_epoch(),
		    nextmap->get_epoch(),
		    NullEvt())));
	    }
	    ret = false;
	    goto out;
	  }
	}
      }
    }

    vector<int> newup, newacting;
    int up_primary, acting_primary;
    nextmap->pg_to_up_acting_osds(
      pg->pg_id.pgid,
      &newup, &up_primary,
      &newacting, &acting_primary);
    pg->handle_advance_map(
      nextmap, lastmap, newup, up_primary,
      newacting, acting_primary, rctx);

    auto oldpool = lastmap->get_pools().find(pg->pg_id.pool());
    auto newpool = nextmap->get_pools().find(pg->pg_id.pool());
    if (oldpool != lastmap->get_pools().end()
        && newpool != nextmap->get_pools().end()) {
      dout(20) << __func__
	       << " new pool opts " << newpool->second.opts
	       << " old pool opts " << oldpool->second.opts
	       << dendl;

      double old_min_interval = 0, new_min_interval = 0;
      oldpool->second.opts.get(pool_opts_t::SCRUB_MIN_INTERVAL, &old_min_interval);
      newpool->second.opts.get(pool_opts_t::SCRUB_MIN_INTERVAL, &new_min_interval);

      double old_max_interval = 0, new_max_interval = 0;
      oldpool->second.opts.get(pool_opts_t::SCRUB_MAX_INTERVAL, &old_max_interval);
      newpool->second.opts.get(pool_opts_t::SCRUB_MAX_INTERVAL, &new_max_interval);

      // Assume if an interval is change from set to unset or vice versa the actual config
      // is different.  Keep it simple even if it is possible to call resched_all_scrub()
      // unnecessarily.
      if (old_min_interval != new_min_interval || old_max_interval != new_max_interval) {
	pg->on_info_history_change();
      }
    }

    if (new_pg_num && old_pg_num != new_pg_num) {
      // check for split
      set<spg_t> children;
      if (pg->pg_id.is_split(
	    old_pg_num,
	    new_pg_num,
	    &children)) {
	split_pgs(
	  pg, children, &new_pgs, lastmap, nextmap,
	  rctx);
      }
    }

    lastmap = nextmap;
    old_pg_num = new_pg_num;
    handle.reset_tp_timeout();
  }
  pg->handle_activate_map(rctx);

  ret = true;
 out:
  if (!new_pgs.empty()) {
    rctx.transaction.register_on_applied(new C_FinishSplits(this, new_pgs));
  }
  return ret;
}

void OSD::consume_map()
{
  ceph_assert(osd_lock.is_locked());
  dout(7) << "consume_map version " << osdmap->get_epoch() << dendl;

  /** make sure the cluster is speaking in SORTBITWISE, because we don't
   *  speak the older sorting version any more. Be careful not to force
   *  a shutdown if we are merely processing old maps, though.
   */
  if (!osdmap->test_flag(CEPH_OSDMAP_SORTBITWISE) && is_active()) {
    derr << __func__ << " SORTBITWISE flag is not set" << dendl;
    ceph_abort();
  }

  service.pre_publish_map(osdmap);
  service.await_reserved_maps();
  service.publish_map(osdmap);

  // prime splits and merges
  set<pair<spg_t,epoch_t>> newly_split;  // splits, and when
  set<pair<spg_t,epoch_t>> merge_pgs;    // merge participants, and when
  for (auto& shard : shards) {
    shard->identify_splits_and_merges(osdmap, &newly_split, &merge_pgs);
  }
  if (!newly_split.empty()) {
    for (auto& shard : shards) {
      shard->prime_splits(osdmap, &newly_split);
    }
    ceph_assert(newly_split.empty());
  }

  // prune sent_ready_to_merge
  service.prune_sent_ready_to_merge(osdmap);

  // FIXME, maybe: We could race against an incoming peering message
  // that instantiates a merge PG after identify_merges() below and
  // never set up its peer to complete the merge.  An OSD restart
  // would clear it up.  This is a hard race to resolve,
  // extraordinarily rare (we only merge PGs that are stable and
  // clean, so it'd have to be an imported PG to an OSD with a
  // slightly stale OSDMap...), so I'm ignoring it for now.  We plan to
  // replace all of this with a seastar-based code soon anyway.
  if (!merge_pgs.empty()) {
    // mark the pgs we already have, or create new and empty merge
    // participants for those we are missing.  do this all under the
    // shard lock so we don't have to worry about racing pg creates
    // via _process.
    for (auto& shard : shards) {
      shard->prime_merges(osdmap, &merge_pgs);
    }
    ceph_assert(merge_pgs.empty());
  }

  service.prune_pg_created();

  unsigned pushes_to_free = 0;
  for (auto& shard : shards) {
    shard->consume_map(osdmap, &pushes_to_free);
  }

  vector<spg_t> pgids;
  _get_pgids(&pgids);

  // count (FIXME, probably during seastar rewrite)
  int num_pg_primary = 0, num_pg_replica = 0, num_pg_stray = 0;
  vector<PGRef> pgs;
  _get_pgs(&pgs);
  for (auto& pg : pgs) {
    // FIXME (probably during seastar rewrite): this is lockless and
    // racy, but we don't want to take pg lock here.
    if (pg->is_primary())
      num_pg_primary++;
    else if (pg->is_replica())
      num_pg_replica++;
    else
      num_pg_stray++;
  }

  {
    // FIXME (as part of seastar rewrite): move to OSDShard
    std::lock_guard l(pending_creates_lock);
    for (auto pg = pending_creates_from_osd.begin();
	 pg != pending_creates_from_osd.end();) {
      if (osdmap->get_pg_acting_rank(pg->first, whoami) < 0) {
	dout(10) << __func__ << " pg " << pg->first << " doesn't map here, "
		 << "discarding pending_create_from_osd" << dendl;
	pg = pending_creates_from_osd.erase(pg);
      } else {
	++pg;
      }
    }
  }

  service.maybe_inject_dispatch_delay();

  dispatch_sessions_waiting_on_map();

  service.maybe_inject_dispatch_delay();

  service.release_reserved_pushes(pushes_to_free);

  // queue null events to push maps down to individual PGs
  for (auto pgid : pgids) {
    enqueue_peering_evt(
      pgid,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  osdmap->get_epoch(),
	  osdmap->get_epoch(),
	  NullEvt())));
  }
  logger->set(l_osd_pg, pgids.size());
  logger->set(l_osd_pg_primary, num_pg_primary);
  logger->set(l_osd_pg_replica, num_pg_replica);
  logger->set(l_osd_pg_stray, num_pg_stray);
}

void OSD::activate_map()
{
  ceph_assert(osd_lock.is_locked());

  dout(7) << "activate_map version " << osdmap->get_epoch() << dendl;

  if (osdmap->test_flag(CEPH_OSDMAP_FULL)) {
    dout(10) << " osdmap flagged full, doing onetime osdmap subscribe" << dendl;
    osdmap_subscribe(osdmap->get_epoch() + 1, false);
  }

  // norecover?
  if (osdmap->test_flag(CEPH_OSDMAP_NORECOVER)) {
    if (!service.recovery_is_paused()) {
      dout(1) << "pausing recovery (NORECOVER flag set)" << dendl;
      service.pause_recovery();
    }
  } else {
    if (service.recovery_is_paused()) {
      dout(1) << "unpausing recovery (NORECOVER flag unset)" << dendl;
      service.unpause_recovery();
    }
  }

  service.activate_map();

  // process waiters
  take_waiters(waiting_for_osdmap);
}

bool OSD::require_mon_peer(const Message *m)
{
  if (!m->get_connection()->peer_is_mon()) {
    dout(0) << "require_mon_peer received from non-mon "
	    << m->get_connection()->get_peer_addr()
	    << " " << *m << dendl;
    return false;
  }
  return true;
}

bool OSD::require_mon_or_mgr_peer(const Message *m)
{
  if (!m->get_connection()->peer_is_mon() &&
      !m->get_connection()->peer_is_mgr()) {
    dout(0) << "require_mon_or_mgr_peer received from non-mon, non-mgr "
	    << m->get_connection()->get_peer_addr()
	    << " " << *m << dendl;
    return false;
  }
  return true;
}

bool OSD::require_osd_peer(const Message *m)
{
  if (!m->get_connection()->peer_is_osd()) {
    dout(0) << "require_osd_peer received from non-osd "
	    << m->get_connection()->get_peer_addr()
	    << " " << *m << dendl;
    return false;
  }
  return true;
}

bool OSD::require_self_aliveness(const Message *m, epoch_t epoch)
{
  epoch_t up_epoch = service.get_up_epoch();
  if (epoch < up_epoch) {
    dout(7) << "from pre-up epoch " << epoch << " < " << up_epoch << dendl;
    return false;
  }

  if (!is_active()) {
    dout(7) << "still in boot state, dropping message " << *m << dendl;
    return false;
  }

  return true;
}

bool OSD::require_same_peer_instance(const Message *m, OSDMapRef& map,
				     bool is_fast_dispatch)
{
  int from = m->get_source().num();

  if (map->is_down(from) ||
      (map->get_cluster_addrs(from) != m->get_source_addrs())) {
    dout(5) << "from dead osd." << from << ", marking down, "
	    << " msg was " << m->get_source_inst().addr
	    << " expected "
	    << (map->is_up(from) ?
		map->get_cluster_addrs(from) : entity_addrvec_t())
	    << dendl;
    ConnectionRef con = m->get_connection();
    con->mark_down();
    auto priv = con->get_priv();
    if (auto s = static_cast<Session*>(priv.get()); s) {
      if (!is_fast_dispatch)
	s->session_dispatch_lock.Lock();
      clear_session_waiting_on_map(s);
      con->set_priv(nullptr);   // break ref <-> session cycle, if any
      s->con.reset();
      if (!is_fast_dispatch)
	s->session_dispatch_lock.Unlock();
    }
    return false;
  }
  return true;
}


/*
 * require that we have same (or newer) map, and that
 * the source is the pg primary.
 */
bool OSD::require_same_or_newer_map(OpRequestRef& op, epoch_t epoch,
				    bool is_fast_dispatch)
{
  const Message *m = op->get_req();
  dout(15) << "require_same_or_newer_map " << epoch
	   << " (i am " << osdmap->get_epoch() << ") " << m << dendl;

  ceph_assert(osd_lock.is_locked());

  // do they have a newer map?
  if (epoch > osdmap->get_epoch()) {
    dout(7) << "waiting for newer map epoch " << epoch
	    << " > my " << osdmap->get_epoch() << " with " << m << dendl;
    wait_for_new_map(op);
    return false;
  }

  if (!require_self_aliveness(op->get_req(), epoch)) {
    return false;
  }

  // ok, our map is same or newer.. do they still exist?
  if (m->get_connection()->get_messenger() == cluster_messenger &&
      !require_same_peer_instance(op->get_req(), osdmap, is_fast_dispatch)) {
    return false;
  }

  return true;
}





// ----------------------------------------
// pg creation

void OSD::split_pgs(
  PG *parent,
  const set<spg_t> &childpgids, set<PGRef> *out_pgs,
  OSDMapRef curmap,
  OSDMapRef nextmap,
  PeeringCtx &rctx)
{
  unsigned pg_num = nextmap->get_pg_num(parent->pg_id.pool());
  parent->update_snap_mapper_bits(parent->get_pgid().get_split_bits(pg_num));

  vector<object_stat_sum_t> updated_stats;
  parent->start_split_stats(childpgids, &updated_stats);

  vector<object_stat_sum_t>::iterator stat_iter = updated_stats.begin();
  for (set<spg_t>::const_iterator i = childpgids.begin();
       i != childpgids.end();
       ++i, ++stat_iter) {
    ceph_assert(stat_iter != updated_stats.end());
    dout(10) << __func__ << " splitting " << *parent << " into " << *i << dendl;
    PG* child = _make_pg(nextmap, *i);
    child->lock(true);
    out_pgs->insert(child);
    child->ch = store->create_new_collection(child->coll);

    {
      uint32_t shard_index = i->hash_to_shard(shards.size());
      assert(NULL != shards[shard_index]);
      store->set_collection_commit_queue(child->coll, &(shards[shard_index]->context_queue));
    }

    unsigned split_bits = i->get_split_bits(pg_num);
    dout(10) << " pg_num is " << pg_num
	     << ", m_seed " << i->ps()
	     << ", split_bits is " << split_bits << dendl;
    parent->split_colls(
      *i,
      split_bits,
      i->ps(),
      &child->get_pool().info,
      rctx.transaction);
    parent->split_into(
      i->pgid,
      child,
      split_bits);

    child->finish_split_stats(*stat_iter, rctx.transaction);
    child->unlock();
  }
  ceph_assert(stat_iter != updated_stats.end());
  parent->finish_split_stats(*stat_iter, rctx.transaction);
}

/*
 * holding osd_lock
 */
void OSD::handle_pg_create(OpRequestRef op)
{
  // NOTE: this can be removed in P release (mimic is the last version to
  // send MOSDPGCreate messages).

  const MOSDPGCreate *m = static_cast<const MOSDPGCreate*>(op->get_req());
  ceph_assert(m->get_type() == MSG_OSD_PG_CREATE);

  dout(10) << "handle_pg_create " << *m << dendl;

  if (!require_mon_peer(op->get_req())) {
    return;
  }

  if (!require_same_or_newer_map(op, m->epoch, false))
    return;

  op->mark_started();

  map<pg_t,utime_t>::const_iterator ci = m->ctimes.begin();
  for (map<pg_t,pg_create_t>::const_iterator p = m->mkpg.begin();
       p != m->mkpg.end();
       ++p, ++ci) {
    ceph_assert(ci != m->ctimes.end() && ci->first == p->first);
    epoch_t created = p->second.created;
    if (p->second.split_bits) // Skip split pgs
      continue;
    pg_t on = p->first;

    if (!osdmap->have_pg_pool(on.pool())) {
      dout(20) << "ignoring pg on deleted pool " << on << dendl;
      continue;
    }

    dout(20) << "mkpg " << on << " e" << created << "@" << ci->second << dendl;

    // is it still ours?
    vector<int> up, acting;
    int up_primary = -1;
    int acting_primary = -1;
    osdmap->pg_to_up_acting_osds(on, &up, &up_primary, &acting, &acting_primary);
    int role = osdmap->calc_pg_role(whoami, acting, acting.size());

    if (acting_primary != whoami) {
      dout(10) << "mkpg " << on << "  not acting_primary (" << acting_primary
	       << "), my role=" << role << ", skipping" << dendl;
      continue;
    }

    spg_t pgid;
    bool mapped = osdmap->get_primary_shard(on, &pgid);
    ceph_assert(mapped);

    PastIntervals pi;
    pg_history_t history;
    build_initial_pg_history(pgid, created, ci->second, &history, &pi);

    // The mon won't resend unless the primary changed, so we ignore
    // same_interval_since.  We'll pass this history with the current
    // epoch as the event.
    if (history.same_primary_since > m->epoch) {
      dout(10) << __func__ << ": got obsolete pg create on pgid "
	       << pgid << " from epoch " << m->epoch
	       << ", primary changed in " << history.same_primary_since
	       << dendl;
      continue;
    }
    enqueue_peering_evt(
      pgid,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  osdmap->get_epoch(),
	  osdmap->get_epoch(),
	  NullEvt(),
	  true,
	  new PGCreateInfo(
	    pgid,
	    osdmap->get_epoch(),
	    history,
	    pi,
	    true)
	  )));
  }

  {
    std::lock_guard l(pending_creates_lock);
    if (pending_creates_from_mon == 0) {
      last_pg_create_epoch = m->epoch;
    }
  }

  maybe_update_heartbeat_peers();
}


// ----------------------------------------
// peering and recovery

PeeringCtx OSD::create_context()
{
  return PeeringCtx();
}

void OSD::dispatch_context_transaction(PeeringCtx &ctx, PG *pg,
                                       ThreadPool::TPHandle *handle)
{
  if (!ctx.transaction.empty() || ctx.transaction.has_contexts()) {
    int tr = store->queue_transaction(
      pg->ch,
      std::move(ctx.transaction), TrackedOpRef(), handle);
    ceph_assert(tr == 0);
    ctx.reset_transaction();
  }
}

void OSD::dispatch_context(PeeringCtx &ctx, PG *pg, OSDMapRef curmap,
                           ThreadPool::TPHandle *handle)
{
  if (!service.get_osdmap()->is_up(whoami)) {
    dout(20) << __func__ << " not up in osdmap" << dendl;
  } else if (!is_active()) {
    dout(20) << __func__ << " not active" << dendl;
  } else {
    do_notifies(ctx.notify_list, curmap);
    do_queries(ctx.query_map, curmap);
    do_infos(ctx.info_map, curmap);
  }
  if ((!ctx.transaction.empty() || ctx.transaction.has_contexts()) && pg) {
    int tr = store->queue_transaction(
      pg->ch,
      std::move(ctx.transaction), TrackedOpRef(),
      handle);
    ceph_assert(tr == 0);
  }
}

/** do_notifies
 * Send an MOSDPGNotify to a primary, with a list of PGs that I have
 * content for, and they are primary for.
 */

void OSD::do_notifies(
  map<int,vector<pair<pg_notify_t,PastIntervals> > >& notify_list,
  OSDMapRef curmap)
{
  for (map<int,
	   vector<pair<pg_notify_t,PastIntervals> > >::iterator it =
	 notify_list.begin();
       it != notify_list.end();
       ++it) {
    if (!curmap->is_up(it->first)) {
      dout(20) << __func__ << " skipping down osd." << it->first << dendl;
      continue;
    }
    ConnectionRef con = service.get_con_osd_cluster(
      it->first, curmap->get_epoch());
    if (!con) {
      dout(20) << __func__ << " skipping osd." << it->first
	       << " (NULL con)" << dendl;
      continue;
    }
    service.maybe_share_map(con.get(), curmap);
    dout(7) << __func__ << " osd." << it->first
	    << " on " << it->second.size() << " PGs" << dendl;
    MOSDPGNotify *m = new MOSDPGNotify(curmap->get_epoch(),
				       std::move(it->second));
    con->send_message(m);
  }
}


/** do_queries
 * send out pending queries for info | summaries
 */
void OSD::do_queries(map<int, map<spg_t,pg_query_t> >& query_map,
		     OSDMapRef curmap)
{
  for (map<int, map<spg_t,pg_query_t> >::iterator pit = query_map.begin();
       pit != query_map.end();
       ++pit) {
    if (!curmap->is_up(pit->first)) {
      dout(20) << __func__ << " skipping down osd." << pit->first << dendl;
      continue;
    }
    int who = pit->first;
    ConnectionRef con = service.get_con_osd_cluster(who, curmap->get_epoch());
    if (!con) {
      dout(20) << __func__ << " skipping osd." << who
	       << " (NULL con)" << dendl;
      continue;
    }
    service.maybe_share_map(con.get(), curmap);
    dout(7) << __func__ << " querying osd." << who
	    << " on " << pit->second.size() << " PGs" << dendl;
    MOSDPGQuery *m = new MOSDPGQuery(curmap->get_epoch(),
				     std::move(pit->second));
    con->send_message(m);
  }
}


void OSD::do_infos(map<int,
		       vector<pair<pg_notify_t, PastIntervals> > >& info_map,
		   OSDMapRef curmap)
{
  for (map<int,
	   vector<pair<pg_notify_t, PastIntervals> > >::iterator p =
	 info_map.begin();
       p != info_map.end();
       ++p) {
    if (!curmap->is_up(p->first)) {
      dout(20) << __func__ << " skipping down osd." << p->first << dendl;
      continue;
    }
    for (vector<pair<pg_notify_t,PastIntervals> >::iterator i = p->second.begin();
	 i != p->second.end();
	 ++i) {
      dout(20) << __func__ << " sending info " << i->first.info
	       << " to shard " << p->first << dendl;
    }
    ConnectionRef con = service.get_con_osd_cluster(
      p->first, curmap->get_epoch());
    if (!con) {
      dout(20) << __func__ << " skipping osd." << p->first
	       << " (NULL con)" << dendl;
      continue;
    }
    service.maybe_share_map(con.get(), curmap);
    MOSDPGInfo *m = new MOSDPGInfo(curmap->get_epoch());
    m->pg_list = p->second;
    con->send_message(m);
  }
  info_map.clear();
}

void OSD::handle_fast_pg_create(MOSDPGCreate2 *m)
{
  dout(7) << __func__ << " " << *m << " from " << m->get_source() << dendl;
  if (!require_mon_peer(m)) {
    m->put();
    return;
  }
  for (auto& p : m->pgs) {
    spg_t pgid = p.first;
    epoch_t created = p.second.first;
    utime_t created_stamp = p.second.second;
    auto q = m->pg_extra.find(pgid);
    if (q == m->pg_extra.end()) {
      dout(20) << __func__ << " " << pgid << " e" << created
	       << "@" << created_stamp
	       << " (no history or past_intervals)" << dendl;
      // pre-octopus ... no pg history.  this can be removed in Q release.
      enqueue_peering_evt(
	pgid,
	PGPeeringEventRef(
	  std::make_shared<PGPeeringEvent>(
	    m->epoch,
	    m->epoch,
	    NullEvt(),
	    true,
	    new PGCreateInfo(
	      pgid,
	      created,
	      pg_history_t(created, created_stamp),
	      PastIntervals(),
	      true)
	    )));
    } else {
      dout(20) << __func__ << " " << pgid << " e" << created
	       << "@" << created_stamp
	       << " history " << q->second.first
	       << " pi " << q->second.second << dendl;
      if (!q->second.second.empty() &&
	  m->epoch < q->second.second.get_bounds().second) {
	clog->error() << "got pg_create on " << pgid << " epoch " << m->epoch
		      << " and unmatched past_intervals " << q->second.second
		      << " (history " << q->second.first << ")";
      } else {
	enqueue_peering_evt(
	  pgid,
	  PGPeeringEventRef(
	    std::make_shared<PGPeeringEvent>(
	      m->epoch,
	      m->epoch,
	      NullEvt(),
	      true,
	      new PGCreateInfo(
		pgid,
		m->epoch,
		q->second.first,
		q->second.second,
		true)
	      )));
      }
    }
  }

  {
    std::lock_guard l(pending_creates_lock);
    if (pending_creates_from_mon == 0) {
      last_pg_create_epoch = m->epoch;
    }
  }

  m->put();
}

void OSD::handle_fast_pg_query(MOSDPGQuery *m)
{
  dout(7) << __func__ << " " << *m << " from " << m->get_source() << dendl;
  if (!require_osd_peer(m)) {
    m->put();
    return;
  }
  int from = m->get_source().num();
  for (auto& p : m->pg_list) {
    enqueue_peering_evt(
      p.first,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  p.second.epoch_sent, p.second.epoch_sent,
	  MQuery(
	    p.first,
	    pg_shard_t(from, p.second.from),
	    p.second,
	    p.second.epoch_sent),
	  false))
      );
  }
  m->put();
}

void OSD::handle_fast_pg_notify(MOSDPGNotify* m)
{
  dout(7) << __func__ << " " << *m << " from " << m->get_source() << dendl;
  if (!require_osd_peer(m)) {
    m->put();
    return;
  }
  int from = m->get_source().num();
  for (auto& p : m->get_pg_list()) {
    spg_t pgid(p.first.info.pgid.pgid, p.first.to);
    enqueue_peering_evt(
      pgid,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  p.first.epoch_sent,
	  p.first.query_epoch,
	  MNotifyRec(
	    pgid, pg_shard_t(from, p.first.from),
	    p.first,
	    m->get_connection()->get_features(),
	    p.second),
	  true,
	  new PGCreateInfo(
	    pgid,
	    p.first.query_epoch,
	    p.first.info.history,
	    p.second,
	    false)
	  )));
  }
  m->put();
}

void OSD::handle_fast_pg_info(MOSDPGInfo* m)
{
  dout(7) << __func__ << " " << *m << " from " << m->get_source() << dendl;
  if (!require_osd_peer(m)) {
    m->put();
    return;
  }
  int from = m->get_source().num();
  for (auto& p : m->pg_list) {
    enqueue_peering_evt(
      spg_t(p.first.info.pgid.pgid, p.first.to),
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  p.first.epoch_sent, p.first.query_epoch,
	  MInfoRec(
	    pg_shard_t(from, p.first.from),
	    p.first.info,
	    p.first.epoch_sent)))
      );
  }
  m->put();
}

void OSD::handle_fast_pg_remove(MOSDPGRemove *m)
{
  dout(7) << __func__ << " " << *m << " from " << m->get_source() << dendl;
  if (!require_osd_peer(m)) {
    m->put();
    return;
  }
  for (auto& pgid : m->pg_list) {
    enqueue_peering_evt(
      pgid,
      PGPeeringEventRef(
	std::make_shared<PGPeeringEvent>(
	  m->get_epoch(), m->get_epoch(),
	  PeeringState::DeleteStart())));
  }
  m->put();
}

void OSD::handle_fast_force_recovery(MOSDForceRecovery *m)
{
  dout(10) << __func__ << " " << *m << dendl;
  if (!require_mon_or_mgr_peer(m)) {
    m->put();
    return;
  }
  epoch_t epoch = get_osdmap_epoch();
  for (auto pgid : m->forced_pgs) {
    if (m->options & OFR_BACKFILL) {
      if (m->options & OFR_CANCEL) {
	enqueue_peering_evt(
	  pgid,
	  PGPeeringEventRef(
	    std::make_shared<PGPeeringEvent>(
	      epoch, epoch,
	      PeeringState::UnsetForceBackfill())));
      } else {
	enqueue_peering_evt(
	  pgid,
	  PGPeeringEventRef(
	    std::make_shared<PGPeeringEvent>(
	      epoch, epoch,
	      PeeringState::SetForceBackfill())));
      }
    } else if (m->options & OFR_RECOVERY) {
      if (m->options & OFR_CANCEL) {
	enqueue_peering_evt(
	  pgid,
	  PGPeeringEventRef(
	    std::make_shared<PGPeeringEvent>(
	      epoch, epoch,
	      PeeringState::UnsetForceRecovery())));
      } else {
	enqueue_peering_evt(
	  pgid,
	  PGPeeringEventRef(
	    std::make_shared<PGPeeringEvent>(
	      epoch, epoch,
	      PeeringState::SetForceRecovery())));
      }
    }
  }
  m->put();
}

void OSD::handle_pg_query_nopg(const MQuery& q)
{
  spg_t pgid = q.pgid;
  dout(10) << __func__ << " " << pgid << dendl;

  OSDMapRef osdmap = get_osdmap();
  if (!osdmap->have_pg_pool(pgid.pool()))
    return;

  dout(10) << " pg " << pgid << " dne" << dendl;
  pg_info_t empty(spg_t(pgid.pgid, q.query.to));
  ConnectionRef con = service.get_con_osd_cluster(q.from.osd, osdmap->get_epoch());
  if (con) {
    Message *m;
    if (q.query.type == pg_query_t::LOG ||
	q.query.type == pg_query_t::FULLLOG) {
      m = new MOSDPGLog(
	q.query.from, q.query.to,
	osdmap->get_epoch(), empty,
	q.query.epoch_sent);
    } else {
      vector<pair<pg_notify_t,PastIntervals>> ls;
      ls.push_back(
	make_pair(
	  pg_notify_t(
	    q.query.from, q.query.to,
	    q.query.epoch_sent,
	    osdmap->get_epoch(),
	    empty),
	  PastIntervals()));
      m = new MOSDPGNotify(osdmap->get_epoch(), std::move(ls));
    }
    service.maybe_share_map(con.get(), osdmap);
    con->send_message(m);
  }
}


// =========================================================
// RECOVERY

void OSDService::_maybe_queue_recovery() {
  ceph_assert(recovery_lock.is_locked_by_me());
  uint64_t available_pushes;
  while (!awaiting_throttle.empty() &&
	 _recover_now(&available_pushes)) {
    uint64_t to_start = std::min(
      available_pushes,
      cct->_conf->osd_recovery_max_single_start);
    _queue_for_recovery(awaiting_throttle.front(), to_start);
    awaiting_throttle.pop_front();
    dout(10) << __func__ << " starting " << to_start
	     << ", recovery_ops_reserved " << recovery_ops_reserved
	     << " -> " << (recovery_ops_reserved + to_start) << dendl;
    recovery_ops_reserved += to_start;
  }
}

bool OSDService::_recover_now(uint64_t *available_pushes)
{
  if (available_pushes)
      *available_pushes = 0;

  if (ceph_clock_now() < defer_recovery_until) {
    dout(15) << __func__ << " defer until " << defer_recovery_until << dendl;
    return false;
  }

  if (recovery_paused) {
    dout(15) << __func__ << " paused" << dendl;
    return false;
  }

  uint64_t max = osd->get_recovery_max_active();
  if (max <= recovery_ops_active + recovery_ops_reserved) {
    dout(15) << __func__ << " active " << recovery_ops_active
	     << " + reserved " << recovery_ops_reserved
	     << " >= max " << max << dendl;
    return false;
  }

  if (available_pushes)
    *available_pushes = max - recovery_ops_active - recovery_ops_reserved;

  return true;
}

void OSD::do_recovery(
  PG *pg, epoch_t queued, uint64_t reserved_pushes,
  ThreadPool::TPHandle &handle)
{
  uint64_t started = 0;

  /*
   * When the value of osd_recovery_sleep is set greater than zero, recovery
   * ops are scheduled after osd_recovery_sleep amount of time from the previous
   * recovery event's schedule time. This is done by adding a
   * recovery_requeue_callback event, which re-queues the recovery op using
   * queue_recovery_after_sleep.
   */
  float recovery_sleep = get_osd_recovery_sleep();
  {
    std::lock_guard l(service.sleep_lock);
    if (recovery_sleep > 0 && service.recovery_needs_sleep) {
      PGRef pgref(pg);
      auto recovery_requeue_callback = new FunctionContext([this, pgref, queued, reserved_pushes](int r) {
        dout(20) << "do_recovery wake up at "
                 << ceph_clock_now()
	         << ", re-queuing recovery" << dendl;
	std::lock_guard l(service.sleep_lock);
        service.recovery_needs_sleep = false;
        service.queue_recovery_after_sleep(pgref.get(), queued, reserved_pushes);
      });

      // This is true for the first recovery op and when the previous recovery op
      // has been scheduled in the past. The next recovery op is scheduled after
      // completing the sleep from now.
      if (service.recovery_schedule_time < ceph_clock_now()) {
        service.recovery_schedule_time = ceph_clock_now();
      }
      service.recovery_schedule_time += recovery_sleep;
      service.sleep_timer.add_event_at(service.recovery_schedule_time,
	                                        recovery_requeue_callback);
      dout(20) << "Recovery event scheduled at "
               << service.recovery_schedule_time << dendl;
      return;
    }
  }

  {
    {
      std::lock_guard l(service.sleep_lock);
      service.recovery_needs_sleep = true;
    }

    if (pg->pg_has_reset_since(queued)) {
      goto out;
    }

    dout(10) << "do_recovery starting " << reserved_pushes << " " << *pg << dendl;
#ifdef DEBUG_RECOVERY_OIDS
    dout(20) << "  active was " << service.recovery_oids[pg->pg_id] << dendl;
#endif

    bool do_unfound = pg->start_recovery_ops(reserved_pushes, handle, &started);
    dout(10) << "do_recovery started " << started << "/" << reserved_pushes 
	     << " on " << *pg << dendl;

    if (do_unfound) {
      PeeringCtx rctx = create_context();
      rctx.handle = &handle;
      pg->find_unfound(queued, rctx);
      dispatch_context(rctx, pg, pg->get_osdmap());
    }
  }

 out:
  ceph_assert(started <= reserved_pushes);
  service.release_reserved_pushes(reserved_pushes);
}

void OSDService::start_recovery_op(PG *pg, const hobject_t& soid)
{
  std::lock_guard l(recovery_lock);
  dout(10) << "start_recovery_op " << *pg << " " << soid
	   << " (" << recovery_ops_active << "/"
	   << osd->get_recovery_max_active() << " rops)"
	   << dendl;
  recovery_ops_active++;

#ifdef DEBUG_RECOVERY_OIDS
  dout(20) << "  active was " << recovery_oids[pg->pg_id] << dendl;
  ceph_assert(recovery_oids[pg->pg_id].count(soid) == 0);
  recovery_oids[pg->pg_id].insert(soid);
#endif
}

void OSDService::finish_recovery_op(PG *pg, const hobject_t& soid, bool dequeue)
{
  std::lock_guard l(recovery_lock);
  dout(10) << "finish_recovery_op " << *pg << " " << soid
	   << " dequeue=" << dequeue
	   << " (" << recovery_ops_active << "/"
	   << osd->get_recovery_max_active() << " rops)"
	   << dendl;

  // adjust count
  ceph_assert(recovery_ops_active > 0);
  recovery_ops_active--;

#ifdef DEBUG_RECOVERY_OIDS
  dout(20) << "  active oids was " << recovery_oids[pg->pg_id] << dendl;
  ceph_assert(recovery_oids[pg->pg_id].count(soid));
  recovery_oids[pg->pg_id].erase(soid);
#endif

  _maybe_queue_recovery();
}

bool OSDService::is_recovery_active()
{
  return local_reserver.has_reservation() || remote_reserver.has_reservation();
}

void OSDService::release_reserved_pushes(uint64_t pushes)
{
  std::lock_guard l(recovery_lock);
  dout(10) << __func__ << "(" << pushes << "), recovery_ops_reserved "
	   << recovery_ops_reserved << " -> " << (recovery_ops_reserved-pushes)
	   << dendl;
  ceph_assert(recovery_ops_reserved >= pushes);
  recovery_ops_reserved -= pushes;
  _maybe_queue_recovery();
}

// =========================================================
// OPS

bool OSD::op_is_discardable(const MOSDOp *op)
{
  // drop client request if they are not connected and can't get the
  // reply anyway.
  if (!op->get_connection()->is_connected()) {
    return true;
  }
  return false;
}

void OSD::enqueue_op(spg_t pg, OpRequestRef&& op, epoch_t epoch)
{
  const utime_t stamp = op->get_req()->get_recv_stamp();
  const utime_t latency = ceph_clock_now() - stamp;
  const unsigned priority = op->get_req()->get_priority();
  const int cost = op->get_req()->get_cost();
  const uint64_t owner = op->get_req()->get_source().num();

  dout(15) << "enqueue_op " << op << " prio " << priority
	   << " cost " << cost
	   << " latency " << latency
	   << " epoch " << epoch
	   << " " << *(op->get_req()) << dendl;
  op->osd_trace.event("enqueue op");
  op->osd_trace.keyval("priority", priority);
  op->osd_trace.keyval("cost", cost);
  op->mark_queued_for_pg();
  logger->tinc(l_osd_op_before_queue_op_lat, latency);
  op_shardedwq.queue(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(new PGOpItem(pg, std::move(op))),
      cost, priority, stamp, owner, epoch));
}

void OSD::enqueue_peering_evt(spg_t pgid, PGPeeringEventRef evt)
{
  dout(15) << __func__ << " " << pgid << " " << evt->get_desc() << dendl;
  op_shardedwq.queue(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(new PGPeeringItem(pgid, evt)),
      10,
      cct->_conf->osd_peering_op_priority,
      utime_t(),
      0,
      evt->get_epoch_sent()));
}

void OSD::enqueue_peering_evt_front(spg_t pgid, PGPeeringEventRef evt)
{
  dout(15) << __func__ << " " << pgid << " " << evt->get_desc() << dendl;
  op_shardedwq.queue_front(
    OpQueueItem(
      unique_ptr<OpQueueItem::OpQueueable>(new PGPeeringItem(pgid, evt)),
      10,
      cct->_conf->osd_peering_op_priority,
      utime_t(),
      0,
      evt->get_epoch_sent()));
}

/*
 * NOTE: dequeue called in worker thread, with pg lock
 */
void OSD::dequeue_op(
  PGRef pg, OpRequestRef op,
  ThreadPool::TPHandle &handle)
{
  const Message *m = op->get_req();

  FUNCTRACE(cct);
  OID_EVENT_TRACE_WITH_MSG(m, "DEQUEUE_OP_BEGIN", false);

  utime_t now = ceph_clock_now();
  op->set_dequeued_time(now);

  utime_t latency = now - m->get_recv_stamp();
  dout(10) << "dequeue_op " << op << " prio " << m->get_priority()
	   << " cost " << m->get_cost()
	   << " latency " << latency
	   << " " << *m
	   << " pg " << *pg << dendl;

  logger->tinc(l_osd_op_before_dequeue_op_lat, latency);

  service.maybe_share_map(m->get_connection().get(),
			  pg->get_osdmap(),
			  op->sent_epoch);

  if (pg->is_deleting())
    return;

  op->mark_reached_pg();
  op->osd_trace.event("dequeue_op");

  pg->do_request(op, handle);

  // finish
  dout(10) << "dequeue_op " << op << " finish" << dendl;
  OID_EVENT_TRACE_WITH_MSG(m, "DEQUEUE_OP_END", false);
}


void OSD::dequeue_peering_evt(
  OSDShard *sdata,
  PG *pg,
  PGPeeringEventRef evt,
  ThreadPool::TPHandle& handle)
{
  PeeringCtx rctx = create_context();
  auto curmap = sdata->get_osdmap();
  bool need_up_thru = false;
  epoch_t same_interval_since = 0;
  if (!pg) {
    if (const MQuery *q = dynamic_cast<const MQuery*>(evt->evt.get())) {
      handle_pg_query_nopg(*q);
    } else {
      derr << __func__ << " unrecognized pg-less event " << evt->get_desc() << dendl;
      ceph_abort();
    }
  } else if (advance_pg(curmap->get_epoch(), pg, handle, rctx)) {
    pg->do_peering_event(evt, rctx);
    if (pg->is_deleted()) {
      pg->unlock();
      return;
    }
    dispatch_context_transaction(rctx, pg, &handle);
    need_up_thru = pg->get_need_up_thru();
    same_interval_since = pg->get_same_interval_since();
    pg->unlock();
  }

  if (need_up_thru) {
    queue_want_up_thru(same_interval_since);
  }
  dispatch_context(rctx, pg, curmap, &handle);

  service.send_pg_temp();
}

void OSD::dequeue_delete(
  OSDShard *sdata,
  PG *pg,
  epoch_t e,
  ThreadPool::TPHandle& handle)
{
  dequeue_peering_evt(
    sdata,
    pg,
    PGPeeringEventRef(
      std::make_shared<PGPeeringEvent>(
	e, e,
	PeeringState::DeleteSome())),
    handle);
}



// --------------------------------

const char** OSD::get_tracked_conf_keys() const
{
  static const char* KEYS[] = {
    "osd_max_backfills",
    "osd_min_recovery_priority",
    "osd_max_trimming_pgs",
    "osd_op_complaint_time",
    "osd_op_log_threshold",
    "osd_op_history_size",
    "osd_op_history_duration",
    "osd_op_history_slow_op_size",
    "osd_op_history_slow_op_threshold",
    "osd_enable_op_tracker",
    "osd_map_cache_size",
    "osd_pg_epoch_max_lag_factor",
    "osd_pg_epoch_persisted_max_stale",
    // clog & admin clog
    "clog_to_monitors",
    "clog_to_syslog",
    "clog_to_syslog_facility",
    "clog_to_syslog_level",
    "osd_objectstore_fuse",
    "clog_to_graylog",
    "clog_to_graylog_host",
    "clog_to_graylog_port",
    "host",
    "fsid",
    "osd_recovery_delay_start",
    "osd_client_message_size_cap",
    "osd_client_message_cap",
    "osd_heartbeat_min_size",
    "osd_heartbeat_interval",
    "osd_object_clean_region_max_num_intervals",
    "osd_scrub_min_interval",
    "osd_scrub_max_interval",
    NULL
  };
  return KEYS;
}

void OSD::handle_conf_change(const ConfigProxy& conf,
			     const std::set <std::string> &changed)
{
  Mutex::Locker l(osd_lock);
  if (changed.count("osd_max_backfills")) {
    service.local_reserver.set_max(cct->_conf->osd_max_backfills);
    service.remote_reserver.set_max(cct->_conf->osd_max_backfills);
  }
  if (changed.count("osd_min_recovery_priority")) {
    service.local_reserver.set_min_priority(cct->_conf->osd_min_recovery_priority);
    service.remote_reserver.set_min_priority(cct->_conf->osd_min_recovery_priority);
  }
  if (changed.count("osd_max_trimming_pgs")) {
    service.snap_reserver.set_max(cct->_conf->osd_max_trimming_pgs);
  }
  if (changed.count("osd_op_complaint_time") ||
      changed.count("osd_op_log_threshold")) {
    op_tracker.set_complaint_and_threshold(cct->_conf->osd_op_complaint_time,
                                           cct->_conf->osd_op_log_threshold);
  }
  if (changed.count("osd_op_history_size") ||
      changed.count("osd_op_history_duration")) {
    op_tracker.set_history_size_and_duration(cct->_conf->osd_op_history_size,
                                             cct->_conf->osd_op_history_duration);
  }
  if (changed.count("osd_op_history_slow_op_size") ||
      changed.count("osd_op_history_slow_op_threshold")) {
    op_tracker.set_history_slow_op_size_and_threshold(cct->_conf->osd_op_history_slow_op_size,
                                                      cct->_conf->osd_op_history_slow_op_threshold);
  }
  if (changed.count("osd_enable_op_tracker")) {
      op_tracker.set_tracking(cct->_conf->osd_enable_op_tracker);
  }
  if (changed.count("osd_map_cache_size")) {
    service.map_cache.set_size(cct->_conf->osd_map_cache_size);
    service.map_bl_cache.set_size(cct->_conf->osd_map_cache_size);
    service.map_bl_inc_cache.set_size(cct->_conf->osd_map_cache_size);
  }
  if (changed.count("clog_to_monitors") ||
      changed.count("clog_to_syslog") ||
      changed.count("clog_to_syslog_level") ||
      changed.count("clog_to_syslog_facility") ||
      changed.count("clog_to_graylog") ||
      changed.count("clog_to_graylog_host") ||
      changed.count("clog_to_graylog_port") ||
      changed.count("host") ||
      changed.count("fsid")) {
    update_log_config();
  }
  if (changed.count("osd_pg_epoch_max_lag_factor")) {
    m_osd_pg_epoch_max_lag_factor = conf.get_val<double>(
      "osd_pg_epoch_max_lag_factor");
  }

#ifdef HAVE_LIBFUSE
  if (changed.count("osd_objectstore_fuse")) {
    if (store) {
      enable_disable_fuse(false);
    }
  }
#endif

  if (changed.count("osd_recovery_delay_start")) {
    service.defer_recovery(cct->_conf->osd_recovery_delay_start);
    service.kick_recovery_queue();
  }

  if (changed.count("osd_client_message_cap")) {
    uint64_t newval = cct->_conf->osd_client_message_cap;
    Messenger::Policy pol = client_messenger->get_policy(entity_name_t::TYPE_CLIENT);
    if (pol.throttler_messages && newval > 0) {
      pol.throttler_messages->reset_max(newval);
    }
  }
  if (changed.count("osd_client_message_size_cap")) {
    uint64_t newval = cct->_conf->osd_client_message_size_cap;
    Messenger::Policy pol = client_messenger->get_policy(entity_name_t::TYPE_CLIENT);
    if (pol.throttler_bytes && newval > 0) {
      pol.throttler_bytes->reset_max(newval);
    }
  }
  if (changed.count("osd_object_clean_region_max_num_intervals")) {
    ObjectCleanRegions::set_max_num_intervals(cct->_conf->osd_object_clean_region_max_num_intervals);
  }

  if (changed.count("osd_scrub_min_interval") ||
      changed.count("osd_scrub_max_interval")) {
    resched_all_scrubs();
    dout(0) << __func__ << ": scrub interval change" << dendl;
  }
  check_config();
}

void OSD::update_log_config()
{
  map<string,string> log_to_monitors;
  map<string,string> log_to_syslog;
  map<string,string> log_channel;
  map<string,string> log_prio;
  map<string,string> log_to_graylog;
  map<string,string> log_to_graylog_host;
  map<string,string> log_to_graylog_port;
  uuid_d fsid;
  string host;

  if (parse_log_client_options(cct, log_to_monitors, log_to_syslog,
			       log_channel, log_prio, log_to_graylog,
			       log_to_graylog_host, log_to_graylog_port,
			       fsid, host) == 0)
    clog->update_config(log_to_monitors, log_to_syslog,
			log_channel, log_prio, log_to_graylog,
			log_to_graylog_host, log_to_graylog_port,
			fsid, host);
  derr << "log_to_monitors " << log_to_monitors << dendl;
}

void OSD::check_config()
{
  // some sanity checks
  if (cct->_conf->osd_map_cache_size <= (int)cct->_conf->osd_pg_epoch_persisted_max_stale + 2) {
    clog->warn() << "osd_map_cache_size (" << cct->_conf->osd_map_cache_size << ")"
		 << " is not > osd_pg_epoch_persisted_max_stale ("
		 << cct->_conf->osd_pg_epoch_persisted_max_stale << ")";
  }
  if (cct->_conf->osd_object_clean_region_max_num_intervals < 0) {
    clog->warn() << "osd_object_clean_region_max_num_intervals (" 
                 << cct->_conf->osd_object_clean_region_max_num_intervals
                << ") is < 0";
  }
}

// --------------------------------

void OSD::get_latest_osdmap()
{
  dout(10) << __func__ << " -- start" << dendl;

  C_SaferCond cond;
  service.objecter->wait_for_latest_osdmap(&cond);
  cond.wait();

  dout(10) << __func__ << " -- finish" << dendl;
}

// --------------------------------

int OSD::init_op_flags(OpRequestRef& op)
{
  const MOSDOp *m = static_cast<const MOSDOp*>(op->get_req());
  vector<OSDOp>::const_iterator iter;

  // client flags have no bearing on whether an op is a read, write, etc.
  op->rmw_flags = 0;

  if (m->has_flag(CEPH_OSD_FLAG_RWORDERED)) {
    op->set_force_rwordered();
  }

  // set bits based on op codes, called methods.
  for (iter = m->ops.begin(); iter != m->ops.end(); ++iter) {
    if ((iter->op.op == CEPH_OSD_OP_WATCH &&
	 iter->op.watch.op == CEPH_OSD_WATCH_OP_PING)) {
      /* This a bit odd.  PING isn't actually a write.  It can't
       * result in an update to the object_info.  PINGs also aren't
       * resent, so there's no reason to write out a log entry.
       *
       * However, we pipeline them behind writes, so let's force
       * the write_ordered flag.
       */
      op->set_force_rwordered();
    } else {
      if (ceph_osd_op_mode_modify(iter->op.op))
	op->set_write();
    }
    if (ceph_osd_op_mode_read(iter->op.op))
      op->set_read();

    // set READ flag if there are src_oids
    if (iter->soid.oid.name.length())
      op->set_read();

    // set PGOP flag if there are PG ops
    if (ceph_osd_op_type_pg(iter->op.op))
      op->set_pg_op();

    if (ceph_osd_op_mode_cache(iter->op.op))
      op->set_cache();

    // check for ec base pool
    int64_t poolid = m->get_pg().pool();
    const pg_pool_t *pool = osdmap->get_pg_pool(poolid);
    if (pool && pool->is_tier()) {
      const pg_pool_t *base_pool = osdmap->get_pg_pool(pool->tier_of);
      if (base_pool && base_pool->require_rollback()) {
        if ((iter->op.op != CEPH_OSD_OP_READ) &&
            (iter->op.op != CEPH_OSD_OP_CHECKSUM) &&
            (iter->op.op != CEPH_OSD_OP_CMPEXT) &&
            (iter->op.op != CEPH_OSD_OP_STAT) &&
            (iter->op.op != CEPH_OSD_OP_ISDIRTY) &&
            (iter->op.op != CEPH_OSD_OP_UNDIRTY) &&
            (iter->op.op != CEPH_OSD_OP_GETXATTR) &&
            (iter->op.op != CEPH_OSD_OP_GETXATTRS) &&
            (iter->op.op != CEPH_OSD_OP_CMPXATTR) &&
            (iter->op.op != CEPH_OSD_OP_ASSERT_VER) &&
            (iter->op.op != CEPH_OSD_OP_LIST_WATCHERS) &&
            (iter->op.op != CEPH_OSD_OP_LIST_SNAPS) &&
            (iter->op.op != CEPH_OSD_OP_SETALLOCHINT) &&
            (iter->op.op != CEPH_OSD_OP_WRITEFULL) &&
            (iter->op.op != CEPH_OSD_OP_ROLLBACK) &&
            (iter->op.op != CEPH_OSD_OP_CREATE) &&
            (iter->op.op != CEPH_OSD_OP_DELETE) &&
            (iter->op.op != CEPH_OSD_OP_SETXATTR) &&
            (iter->op.op != CEPH_OSD_OP_RMXATTR) &&
            (iter->op.op != CEPH_OSD_OP_STARTSYNC) &&
            (iter->op.op != CEPH_OSD_OP_COPY_GET) &&
            (iter->op.op != CEPH_OSD_OP_COPY_FROM)) {
          op->set_promote();
        }
      }
    }

    switch (iter->op.op) {
    case CEPH_OSD_OP_CALL:
      {
	bufferlist::iterator bp = const_cast<bufferlist&>(iter->indata).begin();
	int is_write, is_read;
	string cname, mname;
	bp.copy(iter->op.cls.class_len, cname);
	bp.copy(iter->op.cls.method_len, mname);

	ClassHandler::ClassData *cls;
	int r = class_handler->open_class(cname, &cls);
	if (r) {
	  derr << "class " << cname << " open got " << cpp_strerror(r) << dendl;
	  if (r == -ENOENT)
	    r = -EOPNOTSUPP;
	  else if (r != -EPERM) // propagate permission errors
	    r = -EIO;
	  return r;
	}
	int flags = cls->get_method_flags(mname.c_str());
	if (flags < 0) {
	  if (flags == -ENOENT)
	    r = -EOPNOTSUPP;
	  else
	    r = flags;
	  return r;
	}
	is_read = flags & CLS_METHOD_RD;
	is_write = flags & CLS_METHOD_WR;
        bool is_promote = flags & CLS_METHOD_PROMOTE;

	dout(10) << "class " << cname << " method " << mname << " "
		 << "flags=" << (is_read ? "r" : "")
                             << (is_write ? "w" : "")
                             << (is_promote ? "p" : "")
                 << dendl;
	if (is_read)
	  op->set_class_read();
	if (is_write)
	  op->set_class_write();
        if (is_promote)
          op->set_promote();
        op->add_class(std::move(cname), std::move(mname), is_read, is_write,
                      cls->whitelisted);
	break;
      }

    case CEPH_OSD_OP_WATCH:
      // force the read bit for watch since it is depends on previous
      // watch state (and may return early if the watch exists) or, in
      // the case of ping, is simply a read op.
      op->set_read();
      // fall through
    case CEPH_OSD_OP_NOTIFY:
    case CEPH_OSD_OP_NOTIFY_ACK:
      {
        op->set_promote();
        break;
      }

    case CEPH_OSD_OP_DELETE:
      // if we get a delete with FAILOK we can skip handle cache. without
      // FAILOK we still need to promote (or do something smarter) to
      // determine whether to return ENOENT or 0.
      if (iter == m->ops.begin() &&
	  iter->op.flags == CEPH_OSD_OP_FLAG_FAILOK) {
	op->set_skip_handle_cache();
      }
      // skip promotion when proxying a delete op
      if (m->ops.size() == 1) {
	op->set_skip_promote();
      }
      break;

    case CEPH_OSD_OP_CACHE_TRY_FLUSH:
    case CEPH_OSD_OP_CACHE_FLUSH:
    case CEPH_OSD_OP_CACHE_EVICT:
      // If try_flush/flush/evict is the only op, can skip handle cache.
      if (m->ops.size() == 1) {
	op->set_skip_handle_cache();
      }
      break;

    case CEPH_OSD_OP_READ:
    case CEPH_OSD_OP_SYNC_READ:
    case CEPH_OSD_OP_SPARSE_READ:
    case CEPH_OSD_OP_CHECKSUM:
    case CEPH_OSD_OP_WRITEFULL:
      if (m->ops.size() == 1 &&
          (iter->op.flags & CEPH_OSD_OP_FLAG_FADVISE_NOCACHE ||
           iter->op.flags & CEPH_OSD_OP_FLAG_FADVISE_DONTNEED)) {
        op->set_skip_promote();
      }
      break;

    // force promotion when pin an object in cache tier
    case CEPH_OSD_OP_CACHE_PIN:
      op->set_promote();
      break;

    default:
      break;
    }
  }

  if (op->rmw_flags == 0)
    return -EINVAL;

  return 0;
}

void OSD::set_perf_queries(
    const std::map<OSDPerfMetricQuery, OSDPerfMetricLimits> &queries) {
  dout(10) << "setting " << queries.size() << " queries" << dendl;

  std::list<OSDPerfMetricQuery> supported_queries;
  for (auto &it : queries) {
    auto &query = it.first;
    if (!query.key_descriptor.empty()) {
      supported_queries.push_back(query);
    }
  }
  if (supported_queries.size() < queries.size()) {
    dout(1) << queries.size() - supported_queries.size()
            << " unsupported queries" << dendl;
  }

  {
    Mutex::Locker locker(m_perf_queries_lock);
    m_perf_queries = supported_queries;
    m_perf_limits = queries;
  }

  std::vector<PGRef> pgs;
  _get_pgs(&pgs);
  for (auto& pg : pgs) {
    if (pg->is_primary()) {
      pg->lock();
      pg->set_dynamic_perf_stats_queries(supported_queries);
      pg->unlock();
    }
  }
}

void OSD::get_perf_reports(
    std::map<OSDPerfMetricQuery, OSDPerfMetricReport> *reports) {
  std::vector<PGRef> pgs;
  _get_pgs(&pgs);
  DynamicPerfStats dps;
  for (auto& pg : pgs) {
    if (pg->is_primary()) {
      // m_perf_queries can be modified only in set_perf_queries by mgr client
      // request, and it is protected by by mgr client's lock, which is held
      // when set_perf_queries/get_perf_reports are called, so we may not hold
      // m_perf_queries_lock here.
      DynamicPerfStats pg_dps(m_perf_queries);
      pg->lock();
      pg->get_dynamic_perf_stats(&pg_dps);
      pg->unlock();
      dps.merge(pg_dps);
    }
  }
  dps.add_to_reports(m_perf_limits, reports);
  dout(20) << "reports for " << reports->size() << " queries" << dendl;
}

// =============================================================

#undef dout_context
#define dout_context cct
#undef dout_prefix
#define dout_prefix *_dout << "osd." << osd->get_nodeid() << ":" << shard_id << "." << __func__ << " "

void OSDShard::_attach_pg(OSDShardPGSlot *slot, PG *pg)
{
  dout(10) << pg->pg_id << " " << pg << dendl;
  slot->pg = pg;
  pg->osd_shard = this;
  pg->pg_slot = slot;
  osd->inc_num_pgs();

  slot->epoch = pg->get_osdmap_epoch();
  pg_slots_by_epoch.insert(*slot);
}

void OSDShard::_detach_pg(OSDShardPGSlot *slot)
{
  dout(10) << slot->pg->pg_id << " " << slot->pg << dendl;
  slot->pg->osd_shard = nullptr;
  slot->pg->pg_slot = nullptr;
  slot->pg = nullptr;
  osd->dec_num_pgs();

  pg_slots_by_epoch.erase(pg_slots_by_epoch.iterator_to(*slot));
  slot->epoch = 0;
  if (waiting_for_min_pg_epoch) {
    min_pg_epoch_cond.notify_all();
  }
}

void OSDShard::update_pg_epoch(OSDShardPGSlot *slot, epoch_t e)
{
  std::lock_guard l(shard_lock);
  dout(30) << "min was " << pg_slots_by_epoch.begin()->epoch
	   << " on " << pg_slots_by_epoch.begin()->pg->pg_id << dendl;
  pg_slots_by_epoch.erase(pg_slots_by_epoch.iterator_to(*slot));
  dout(20) << slot->pg->pg_id << " " << slot->epoch << " -> " << e << dendl;
  slot->epoch = e;
  pg_slots_by_epoch.insert(*slot);
  dout(30) << "min is now " << pg_slots_by_epoch.begin()->epoch
	   << " on " << pg_slots_by_epoch.begin()->pg->pg_id << dendl;
  if (waiting_for_min_pg_epoch) {
    min_pg_epoch_cond.notify_all();
  }
}

epoch_t OSDShard::get_min_pg_epoch()
{
  std::lock_guard l(shard_lock);
  auto p = pg_slots_by_epoch.begin();
  if (p == pg_slots_by_epoch.end()) {
    return 0;
  }
  return p->epoch;
}

void OSDShard::wait_min_pg_epoch(epoch_t need)
{
  std::unique_lock l{shard_lock};
  ++waiting_for_min_pg_epoch;
  min_pg_epoch_cond.wait(l, [need, this] {
    if (pg_slots_by_epoch.empty()) {
      return true;
    } else if (pg_slots_by_epoch.begin()->epoch >= need) {
      return true;
    } else {
      dout(10) << need << " waiting on "
	       << pg_slots_by_epoch.begin()->epoch << dendl;
      return false;
    }
  });
  --waiting_for_min_pg_epoch;
}

epoch_t OSDShard::get_max_waiting_epoch()
{
  std::lock_guard l(shard_lock);
  epoch_t r = 0;
  for (auto& i : pg_slots) {
    if (!i.second->waiting_peering.empty()) {
      r = std::max(r, i.second->waiting_peering.rbegin()->first);
    }
  }
  return r;
}

void OSDShard::consume_map(
  OSDMapRef& new_osdmap,
  unsigned *pushes_to_free)
{
  std::lock_guard l(shard_lock);
  OSDMapRef old_osdmap;
  {
    std::lock_guard l(osdmap_lock);
    old_osdmap = std::move(shard_osdmap);
    shard_osdmap = new_osdmap;
  }
  dout(10) << new_osdmap->get_epoch()
           << " (was " << (old_osdmap ? old_osdmap->get_epoch() : 0) << ")"
	   << dendl;
  bool queued = false;

  // check slots
  auto p = pg_slots.begin();
  while (p != pg_slots.end()) {
    OSDShardPGSlot *slot = p->second.get();
    const spg_t& pgid = p->first;
    dout(20) << __func__ << " " << pgid << dendl;
    if (!slot->waiting_for_split.empty()) {
      dout(20) << __func__ << "  " << pgid
	       << " waiting for split " << slot->waiting_for_split << dendl;
      ++p;
      continue;
    }
    if (slot->waiting_for_merge_epoch > new_osdmap->get_epoch()) {
      dout(20) << __func__ << "  " << pgid
	       << " waiting for merge by epoch " << slot->waiting_for_merge_epoch
	       << dendl;
      ++p;
      continue;
    }
    if (!slot->waiting_peering.empty()) {
      epoch_t first = slot->waiting_peering.begin()->first;
      if (first <= new_osdmap->get_epoch()) {
	dout(20) << __func__ << "  " << pgid
		 << " pending_peering first epoch " << first
		 << " <= " << new_osdmap->get_epoch() << ", requeueing" << dendl;
	_wake_pg_slot(pgid, slot);
	queued = true;
      }
      ++p;
      continue;
    }
    if (!slot->waiting.empty()) {
      if (new_osdmap->is_up_acting_osd_shard(pgid, osd->get_nodeid())) {
	dout(20) << __func__ << "  " << pgid << " maps to us, keeping"
		 << dendl;
	++p;
	continue;
      }
      while (!slot->waiting.empty() &&
	     slot->waiting.front().get_map_epoch() <= new_osdmap->get_epoch()) {
	auto& qi = slot->waiting.front();
	dout(20) << __func__ << "  " << pgid
		 << " waiting item " << qi
		 << " epoch " << qi.get_map_epoch()
		 << " <= " << new_osdmap->get_epoch()
		 << ", "
		 << (qi.get_map_epoch() < new_osdmap->get_epoch() ? "stale" :
		     "misdirected")
		 << ", dropping" << dendl;
        *pushes_to_free += qi.get_reserved_pushes();
	slot->waiting.pop_front();
      }
    }
    if (slot->waiting.empty() &&
	slot->num_running == 0 &&
	slot->waiting_for_split.empty() &&
	!slot->pg) {
      dout(20) << __func__ << "  " << pgid << " empty, pruning" << dendl;
      p = pg_slots.erase(p);
      continue;
    }

    ++p;
  }
  if (queued) {
    std::lock_guard l{sdata_wait_lock};
    sdata_cond.notify_one();
  }
}

void OSDShard::_wake_pg_slot(
  spg_t pgid,
  OSDShardPGSlot *slot)
{
  dout(20) << __func__ << " " << pgid
	   << " to_process " << slot->to_process
	   << " waiting " << slot->waiting
	   << " waiting_peering " << slot->waiting_peering << dendl;
  for (auto i = slot->to_process.rbegin();
       i != slot->to_process.rend();
       ++i) {
    _enqueue_front(std::move(*i), osd->op_prio_cutoff);
  }
  slot->to_process.clear();
  for (auto i = slot->waiting.rbegin();
       i != slot->waiting.rend();
       ++i) {
    _enqueue_front(std::move(*i), osd->op_prio_cutoff);
  }
  slot->waiting.clear();
  for (auto i = slot->waiting_peering.rbegin();
       i != slot->waiting_peering.rend();
       ++i) {
    // this is overkill; we requeue everything, even if some of these
    // items are waiting for maps we don't have yet.  FIXME, maybe,
    // someday, if we decide this inefficiency matters
    for (auto j = i->second.rbegin(); j != i->second.rend(); ++j) {
      _enqueue_front(std::move(*j), osd->op_prio_cutoff);
    }
  }
  slot->waiting_peering.clear();
  ++slot->requeue_seq;
}

void OSDShard::identify_splits_and_merges(
  const OSDMapRef& as_of_osdmap,
  set<pair<spg_t,epoch_t>> *split_pgs,
  set<pair<spg_t,epoch_t>> *merge_pgs)
{
  std::lock_guard l(shard_lock);
  if (shard_osdmap) {
    for (auto& i : pg_slots) {
      const spg_t& pgid = i.first;
      auto *slot = i.second.get();
      if (slot->pg) {
	osd->service.identify_splits_and_merges(
	  shard_osdmap, as_of_osdmap, pgid,
	  split_pgs, merge_pgs);
      } else if (!slot->waiting_for_split.empty()) {
	osd->service.identify_splits_and_merges(
	  shard_osdmap, as_of_osdmap, pgid,
	  split_pgs, nullptr);
      } else {
	dout(20) << __func__ << " slot " << pgid
		 << " has no pg and waiting_for_split "
		 << slot->waiting_for_split << dendl;
      }
    }
  }
}

void OSDShard::prime_splits(const OSDMapRef& as_of_osdmap,
			    set<pair<spg_t,epoch_t>> *pgids)
{
  std::lock_guard l(shard_lock);
  _prime_splits(pgids);
  if (shard_osdmap->get_epoch() > as_of_osdmap->get_epoch()) {
    set<pair<spg_t,epoch_t>> newer_children;
    for (auto i : *pgids) {
      osd->service.identify_splits_and_merges(
	as_of_osdmap, shard_osdmap, i.first,
	&newer_children, nullptr);
    }
    newer_children.insert(pgids->begin(), pgids->end());
    dout(10) << "as_of_osdmap " << as_of_osdmap->get_epoch() << " < shard "
	     << shard_osdmap->get_epoch() << ", new children " << newer_children
	     << dendl;
    _prime_splits(&newer_children);
    // note: we don't care what is left over here for other shards.
    // if this shard is ahead of us and one isn't, e.g., one thread is
    // calling into prime_splits via _process (due to a newly created
    // pg) and this shard has a newer map due to a racing consume_map,
    // then any grandchildren left here will be identified (or were
    // identified) when the slower shard's osdmap is advanced.
    // _prime_splits() will tolerate the case where the pgid is
    // already primed.
  }
}

void OSDShard::_prime_splits(set<pair<spg_t,epoch_t>> *pgids)
{
  dout(10) << *pgids << dendl;
  auto p = pgids->begin();
  while (p != pgids->end()) {
    unsigned shard_index = p->first.hash_to_shard(osd->num_shards);
    if (shard_index == shard_id) {
      auto r = pg_slots.emplace(p->first, nullptr);
      if (r.second) {
	dout(10) << "priming slot " << p->first << " e" << p->second << dendl;
	r.first->second = make_unique<OSDShardPGSlot>();
	r.first->second->waiting_for_split.insert(p->second);
      } else {
	auto q = r.first;
	ceph_assert(q != pg_slots.end());
	dout(10) << "priming (existing) slot " << p->first << " e" << p->second
		 << dendl;
	q->second->waiting_for_split.insert(p->second);
      }
      p = pgids->erase(p);
    } else {
      ++p;
    }
  }
}

void OSDShard::prime_merges(const OSDMapRef& as_of_osdmap,
			    set<pair<spg_t,epoch_t>> *merge_pgs)
{
  std::lock_guard l(shard_lock);
  dout(20) << __func__ << " checking shard " << shard_id
	   << " for remaining merge pgs " << merge_pgs << dendl;
  auto p = merge_pgs->begin();
  while (p != merge_pgs->end()) {
    spg_t pgid = p->first;
    epoch_t epoch = p->second;
    unsigned shard_index = pgid.hash_to_shard(osd->num_shards);
    if (shard_index != shard_id) {
      ++p;
      continue;
    }
    OSDShardPGSlot *slot;
    auto r = pg_slots.emplace(pgid, nullptr);
    if (r.second) {
      r.first->second = make_unique<OSDShardPGSlot>();
    }
    slot = r.first->second.get();
    if (slot->pg) {
      // already have pg
      dout(20) << __func__ << "  have merge participant pg " << pgid
	       << " " << slot->pg << dendl;
    } else if (!slot->waiting_for_split.empty() &&
	       *slot->waiting_for_split.begin() < epoch) {
      dout(20) << __func__ << "  pending split on merge participant pg " << pgid
	       << " " << slot->waiting_for_split << dendl;
    } else {
      dout(20) << __func__ << "  creating empty merge participant " << pgid
	       << " for merge in " << epoch << dendl;
      // leave history zeroed; PG::merge_from() will fill it in.
      pg_history_t history;
      PGCreateInfo cinfo(pgid, epoch - 1,
			 history, PastIntervals(), false);
      PGRef pg = osd->handle_pg_create_info(shard_osdmap, &cinfo);
      _attach_pg(r.first->second.get(), pg.get());
      _wake_pg_slot(pgid, slot);
      pg->unlock();
    }
    // mark slot for merge
    dout(20) << __func__ << "  marking merge participant " << pgid << dendl;
    slot->waiting_for_merge_epoch = epoch;
    p = merge_pgs->erase(p);
  }
}

void OSDShard::register_and_wake_split_child(PG *pg)
{
  epoch_t epoch;
  {
    std::lock_guard l(shard_lock);
    dout(10) << pg->pg_id << " " << pg << dendl;
    auto p = pg_slots.find(pg->pg_id);
    ceph_assert(p != pg_slots.end());
    auto *slot = p->second.get();
    dout(20) << pg->pg_id << " waiting_for_split " << slot->waiting_for_split
	     << dendl;
    ceph_assert(!slot->pg);
    ceph_assert(!slot->waiting_for_split.empty());
    _attach_pg(slot, pg);

    epoch = pg->get_osdmap_epoch();
    ceph_assert(slot->waiting_for_split.count(epoch));
    slot->waiting_for_split.erase(epoch);
    if (slot->waiting_for_split.empty()) {
      _wake_pg_slot(pg->pg_id, slot);
    } else {
      dout(10) << __func__ << " still waiting for split on "
	       << slot->waiting_for_split << dendl;
    }
  }

  // kick child to ensure it pulls up to the latest osdmap
  osd->enqueue_peering_evt(
    pg->pg_id,
    PGPeeringEventRef(
      std::make_shared<PGPeeringEvent>(
	epoch,
	epoch,
	NullEvt())));

  std::lock_guard l{sdata_wait_lock};
  sdata_cond.notify_one();
}

void OSDShard::unprime_split_children(spg_t parent, unsigned old_pg_num)
{
  std::lock_guard l(shard_lock);
  vector<spg_t> to_delete;
  for (auto& i : pg_slots) {
    if (i.first != parent &&
	i.first.get_ancestor(old_pg_num) == parent) {
      dout(10) << __func__ << " parent " << parent << " clearing " << i.first
	       << dendl;
      _wake_pg_slot(i.first, i.second.get());
      to_delete.push_back(i.first);
    }
  }
  for (auto pgid : to_delete) {
    pg_slots.erase(pgid);
  }
}


// =============================================================

#undef dout_context
#define dout_context osd->cct
#undef dout_prefix
#define dout_prefix *_dout << "osd." << osd->whoami << " op_wq "

void OSD::ShardedOpWQ::_add_slot_waiter(
  spg_t pgid,
  OSDShardPGSlot *slot,
  OpQueueItem&& qi)
{
  if (qi.is_peering()) {
    dout(20) << __func__ << " " << pgid
	     << " peering, item epoch is "
	     << qi.get_map_epoch()
	     << ", will wait on " << qi << dendl;
    slot->waiting_peering[qi.get_map_epoch()].push_back(std::move(qi));
  } else {
    dout(20) << __func__ << " " << pgid
	     << " item epoch is "
	     << qi.get_map_epoch()
	     << ", will wait on " << qi << dendl;
    slot->waiting.push_back(std::move(qi));
  }
}

#undef dout_prefix
#define dout_prefix *_dout << "osd." << osd->whoami << " op_wq(" << shard_index << ") "

void OSD::ShardedOpWQ::_process(uint32_t thread_index, heartbeat_handle_d *hb)
{
  uint32_t shard_index = thread_index % osd->num_shards;
  auto& sdata = osd->shards[shard_index];
  ceph_assert(sdata);

  // If all threads of shards do oncommits, there is a out-of-order
  // problem.  So we choose the thread which has the smallest
  // thread_index(thread_index < num_shards) of shard to do oncommit
  // callback.
  bool is_smallest_thread_index = thread_index < osd->num_shards;

  // peek at spg_t
  sdata->shard_lock.lock();
  if (sdata->pqueue->empty() &&
      (!is_smallest_thread_index || sdata->context_queue.empty())) {
    std::unique_lock wait_lock{sdata->sdata_wait_lock};
    if (is_smallest_thread_index && !sdata->context_queue.empty()) {
      // we raced with a context_queue addition, don't wait
      wait_lock.unlock();
    } else if (!sdata->stop_waiting) {
      dout(20) << __func__ << " empty q, waiting" << dendl;
      osd->cct->get_heartbeat_map()->clear_timeout(hb);
      sdata->shard_lock.unlock();
      sdata->sdata_cond.wait(wait_lock);
      wait_lock.unlock();
      sdata->shard_lock.lock();
      if (sdata->pqueue->empty() &&
         !(is_smallest_thread_index && !sdata->context_queue.empty())) {
	sdata->shard_lock.unlock();
	return;
      }
      osd->cct->get_heartbeat_map()->reset_timeout(hb,
	  osd->cct->_conf->threadpool_default_timeout, 0);
    } else {
      dout(20) << __func__ << " need return immediately" << dendl;
      wait_lock.unlock();
      sdata->shard_lock.unlock();
      return;
    }
  }

  list<Context *> oncommits;
  if (is_smallest_thread_index && !sdata->context_queue.empty()) {
    sdata->context_queue.swap(oncommits);
  }

  if (sdata->pqueue->empty()) {
    if (osd->is_stopping()) {
      sdata->shard_lock.unlock();
      for (auto c : oncommits) {
	dout(10) << __func__ << " discarding in-flight oncommit " << c << dendl;
	delete c;
      }
      return;    // OSD shutdown, discard.
    }
    sdata->shard_lock.unlock();
    handle_oncommits(oncommits);
    return;
  }

  OpQueueItem item = sdata->pqueue->dequeue();
  if (osd->is_stopping()) {
    sdata->shard_lock.unlock();
    for (auto c : oncommits) {
      dout(10) << __func__ << " discarding in-flight oncommit " << c << dendl;
      delete c;
    }
    return;    // OSD shutdown, discard.
  }

  const auto token = item.get_ordering_token();
  auto r = sdata->pg_slots.emplace(token, nullptr);
  if (r.second) {
    r.first->second = make_unique<OSDShardPGSlot>();
  }
  OSDShardPGSlot *slot = r.first->second.get();
  dout(20) << __func__ << " " << token
	   << (r.second ? " (new)" : "")
	   << " to_process " << slot->to_process
	   << " waiting " << slot->waiting
	   << " waiting_peering " << slot->waiting_peering
	   << dendl;
  slot->to_process.push_back(std::move(item));
  dout(20) << __func__ << " " << slot->to_process.back()
	   << " queued" << dendl;

 retry_pg:
  PGRef pg = slot->pg;

  // lock pg (if we have it)
  if (pg) {
    // note the requeue seq now...
    uint64_t requeue_seq = slot->requeue_seq;
    ++slot->num_running;

    sdata->shard_lock.unlock();
    osd->service.maybe_inject_dispatch_delay();
    pg->lock();
    osd->service.maybe_inject_dispatch_delay();
    sdata->shard_lock.lock();

    auto q = sdata->pg_slots.find(token);
    if (q == sdata->pg_slots.end()) {
      // this can happen if we race with pg removal.
      dout(20) << __func__ << " slot " << token << " no longer there" << dendl;
      pg->unlock();
      sdata->shard_lock.unlock();
      handle_oncommits(oncommits);
      return;
    }
    slot = q->second.get();
    --slot->num_running;

    if (slot->to_process.empty()) {
      // raced with _wake_pg_slot or consume_map
      dout(20) << __func__ << " " << token
	       << " nothing queued" << dendl;
      pg->unlock();
      sdata->shard_lock.unlock();
      handle_oncommits(oncommits);
      return;
    }
    if (requeue_seq != slot->requeue_seq) {
      dout(20) << __func__ << " " << token
	       << " requeue_seq " << slot->requeue_seq << " > our "
	       << requeue_seq << ", we raced with _wake_pg_slot"
	       << dendl;
      pg->unlock();
      sdata->shard_lock.unlock();
      handle_oncommits(oncommits);
      return;
    }
    if (slot->pg != pg) {
      // this can happen if we race with pg removal.
      dout(20) << __func__ << " slot " << token << " no longer attached to "
	       << pg << dendl;
      pg->unlock();
      goto retry_pg;
    }
  }

  dout(20) << __func__ << " " << token
	   << " to_process " << slot->to_process
	   << " waiting " << slot->waiting
	   << " waiting_peering " << slot->waiting_peering << dendl;

  ThreadPool::TPHandle tp_handle(osd->cct, hb, timeout_interval,
				 suicide_interval);

  // take next item
  auto qi = std::move(slot->to_process.front());
  slot->to_process.pop_front();
  dout(20) << __func__ << " " << qi << " pg " << pg << dendl;
  set<pair<spg_t,epoch_t>> new_children;
  OSDMapRef osdmap;

  while (!pg) {
    // should this pg shard exist on this osd in this (or a later) epoch?
    osdmap = sdata->shard_osdmap;
    const PGCreateInfo *create_info = qi.creates_pg();
    if (!slot->waiting_for_split.empty()) {
      dout(20) << __func__ << " " << token
	       << " splitting " << slot->waiting_for_split << dendl;
      _add_slot_waiter(token, slot, std::move(qi));
    } else if (qi.get_map_epoch() > osdmap->get_epoch()) {
      dout(20) << __func__ << " " << token
	       << " map " << qi.get_map_epoch() << " > "
	       << osdmap->get_epoch() << dendl;
      _add_slot_waiter(token, slot, std::move(qi));
    } else if (qi.is_peering()) {
      if (!qi.peering_requires_pg()) {
	// for pg-less events, we run them under the ordering lock, since
	// we don't have the pg lock to keep them ordered.
	qi.run(osd, sdata, pg, tp_handle);
      } else if (osdmap->is_up_acting_osd_shard(token, osd->whoami)) {
	if (create_info) {
	  if (create_info->by_mon &&
	      osdmap->get_pg_acting_primary(token.pgid) != osd->whoami) {
	    dout(20) << __func__ << " " << token
		     << " no pg, no longer primary, ignoring mon create on "
		     << qi << dendl;
	  } else {
	    dout(20) << __func__ << " " << token
		     << " no pg, should create on " << qi << dendl;
	    pg = osd->handle_pg_create_info(osdmap, create_info);
	    if (pg) {
	      // we created the pg! drop out and continue "normally"!
	      sdata->_attach_pg(slot, pg.get());
	      sdata->_wake_pg_slot(token, slot);

	      // identify split children between create epoch and shard epoch.
	      osd->service.identify_splits_and_merges(
		pg->get_osdmap(), osdmap, pg->pg_id, &new_children, nullptr);
	      sdata->_prime_splits(&new_children);
	      // distribute remaining split children to other shards below!
	      break;
	    }
	    dout(20) << __func__ << " ignored create on " << qi << dendl;
	  }
	} else {
	  dout(20) << __func__ << " " << token
		   << " no pg, peering, !create, discarding " << qi << dendl;
	}
      } else {
	dout(20) << __func__ << " " << token
		 << " no pg, peering, doesn't map here e" << osdmap->get_epoch()
		 << ", discarding " << qi
		 << dendl;
      }
    } else if (osdmap->is_up_acting_osd_shard(token, osd->whoami)) {
      dout(20) << __func__ << " " << token
	       << " no pg, should exist e" << osdmap->get_epoch()
	       << ", will wait on " << qi << dendl;
      _add_slot_waiter(token, slot, std::move(qi));
    } else {
      dout(20) << __func__ << " " << token
	       << " no pg, shouldn't exist e" << osdmap->get_epoch()
	       << ", dropping " << qi << dendl;
      // share map with client?
      if (std::optional<OpRequestRef> _op = qi.maybe_get_op()) {
	osd->service.maybe_share_map((*_op)->get_req()->get_connection().get(),
				     sdata->shard_osdmap,
				     (*_op)->sent_epoch);
      }
      unsigned pushes_to_free = qi.get_reserved_pushes();
      if (pushes_to_free > 0) {
	sdata->shard_lock.unlock();
	osd->service.release_reserved_pushes(pushes_to_free);
	handle_oncommits(oncommits);
	return;
      }
    }
    sdata->shard_lock.unlock();
    handle_oncommits(oncommits);
    return;
  }
  if (qi.is_peering()) {
    OSDMapRef osdmap = sdata->shard_osdmap;
    if (qi.get_map_epoch() > osdmap->get_epoch()) {
      _add_slot_waiter(token, slot, std::move(qi));
      sdata->shard_lock.unlock();
      pg->unlock();
      handle_oncommits(oncommits);
      return;
    }
  }
  sdata->shard_lock.unlock();

  if (!new_children.empty()) {
    for (auto shard : osd->shards) {
      shard->prime_splits(osdmap, &new_children);
    }
    ceph_assert(new_children.empty());
  }

  // osd_opwq_process marks the point at which an operation has been dequeued
  // and will begin to be handled by a worker thread.
  {
#ifdef WITH_LTTNG
    osd_reqid_t reqid;
    if (std::optional<OpRequestRef> _op = qi.maybe_get_op()) {
      reqid = (*_op)->get_reqid();
    }
#endif
    tracepoint(osd, opwq_process_start, reqid.name._type,
        reqid.name._num, reqid.tid, reqid.inc);
  }

  lgeneric_subdout(osd->cct, osd, 30) << "dequeue status: ";
  Formatter *f = Formatter::create("json");
  f->open_object_section("q");
  dump(f);
  f->close_section();
  f->flush(*_dout);
  delete f;
  *_dout << dendl;

  qi.run(osd, sdata, pg, tp_handle);

  {
#ifdef WITH_LTTNG
    osd_reqid_t reqid;
    if (std::optional<OpRequestRef> _op = qi.maybe_get_op()) {
      reqid = (*_op)->get_reqid();
    }
#endif
    tracepoint(osd, opwq_process_finish, reqid.name._type,
        reqid.name._num, reqid.tid, reqid.inc);
  }

  handle_oncommits(oncommits);
}

void OSD::ShardedOpWQ::_enqueue(OpQueueItem&& item) {
  uint32_t shard_index =
    item.get_ordering_token().hash_to_shard(osd->shards.size());

  OSDShard* sdata = osd->shards[shard_index];
  assert (NULL != sdata);
  unsigned priority = item.get_priority();
  unsigned cost = item.get_cost();
  sdata->shard_lock.lock();

  dout(20) << __func__ << " " << item << dendl;
  if (priority >= osd->op_prio_cutoff)
    sdata->pqueue->enqueue_strict(
      item.get_owner(), priority, std::move(item));
  else
    sdata->pqueue->enqueue(
      item.get_owner(), priority, cost, std::move(item));
  sdata->shard_lock.unlock();

  std::lock_guard l{sdata->sdata_wait_lock};
  sdata->sdata_cond.notify_one();
}

void OSD::ShardedOpWQ::_enqueue_front(OpQueueItem&& item)
{
  auto shard_index = item.get_ordering_token().hash_to_shard(osd->shards.size());
  auto& sdata = osd->shards[shard_index];
  ceph_assert(sdata);
  sdata->shard_lock.lock();
  auto p = sdata->pg_slots.find(item.get_ordering_token());
  if (p != sdata->pg_slots.end() &&
      !p->second->to_process.empty()) {
    // we may be racing with _process, which has dequeued a new item
    // from pqueue, put it on to_process, and is now busy taking the
    // pg lock.  ensure this old requeued item is ordered before any
    // such newer item in to_process.
    p->second->to_process.push_front(std::move(item));
    item = std::move(p->second->to_process.back());
    p->second->to_process.pop_back();
    dout(20) << __func__
	     << " " << p->second->to_process.front()
	     << " shuffled w/ " << item << dendl;
  } else {
    dout(20) << __func__ << " " << item << dendl;
  }
  sdata->_enqueue_front(std::move(item), osd->op_prio_cutoff);
  sdata->shard_lock.unlock();
  std::lock_guard l{sdata->sdata_wait_lock};
  sdata->sdata_cond.notify_one();
}

namespace ceph { 
namespace osd_cmds { 

int heap(CephContext& cct, const cmdmap_t& cmdmap, Formatter& f,
	 std::ostream& os)
{
  if (!ceph_using_tcmalloc()) {
        os << "could not issue heap profiler command -- not using tcmalloc!";
        return -EOPNOTSUPP;
  }
  
  string cmd;
  if (!cmd_getval(&cct, cmdmap, "heapcmd", cmd)) {
        os << "unable to get value for command \"" << cmd << "\"";
       return -EINVAL;
  }
  
  std::vector<std::string> cmd_vec;
  get_str_vec(cmd, cmd_vec);

  string val;
  if (cmd_getval(&cct, cmdmap, "value", val)) {
    cmd_vec.push_back(val);
  }
  
  ceph_heap_profiler_handle_command(cmd_vec, os);
  
  return 0;
}
 
}} // namespace ceph::osd_cmds


std::ostream& operator<<(std::ostream& out, const io_queue& q) {
  switch(q) {
  case io_queue::prioritized:
    out << "prioritized";
    break;
  case io_queue::weightedpriority:
    out << "weightedpriority";
    break;
  case io_queue::mclock_opclass:
    out << "mclock_opclass";
    break;
  case io_queue::mclock_client:
    out << "mclock_client";
    break;
  }
  return out;
}
