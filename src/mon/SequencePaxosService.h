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

#ifndef CEPH_SEQUENCEPAXOSSERVICE_H
#define CEPH_SEQUENCEPAXOSSERVICE_H

#include "PaxosService.h"


class SequencePaxosService : public PaxosService {
public:
  /**
   * The version to trim to. If zero, we assume there is no version to be
   * trimmed; otherwise, we assume we should trim to the version held by
   * this variable.
   */
  version_t trim_version;

  SequencePaxosService(Monitor *mn, Paxos *p, string name)
    : PaxosService(mn, p, name),
      trim_version(0),
      first_committed_name("first_committed"),
      full_prefix_name("full"),
      full_latest_name("latest"),
      cached_first_committed(0)
  {}

  virtual void refresh(bool *need_bootstrap);

  /**
   * Scrub our versions after we convert the store from the old layout to
   * the new k/v store.
   */
  virtual void scrub();

private:
  const string first_committed_name;
  const string full_prefix_name;
  const string full_latest_name;

  version_t cached_first_committed;

public:
  /**
   * @defgroup PaxosService_h_Trim
   * @{
   */
  /**
   * Auxiliary function to trim our state from version @from to version @to,
   * not including; i.e., the interval [from, to[
   *
   * @param t The transaction to which we will add the trim operations.
   * @param from the lower limit of the interval to be trimmed
   * @param to the upper limit of the interval to be trimmed (not including)
   */
  void trim(MonitorDBStore::Transaction *t, version_t from, version_t to);
  /**
   * Trim our log. This implies getting rid of versions on the k/v store.
   * Services implementing us don't have to implement this function if they
   * don't want to, but we won't implement it for them either.
   *
   * This function had to be inheritted from the Paxos, since the existing
   * services made use of it. This function should be tuned for each service's
   * needs. We have it in this interface to make sure its usage and purpose is
   * well understood by the underlying services.
   *
   * @param first The version that should become the first one in the log.
   * @param force Optional. Each service may use it as it sees fit, but the
   *		  expected behavior is that, when 'true', we will remove all
   *		  the log versions even if we don't have a full map in store.
   */
  virtual void encode_trim(MonitorDBStore::Transaction *t);

  virtual void _encode_pending(MonitorDBStore::Transaction *t);

  /**
   *
   */
  virtual bool should_trim() {
    bool want_trim = service_should_trim();

    if (!want_trim)
      return false;

    if (g_conf->paxos_service_trim_min > 0) {
      version_t trim_to = get_trim_to();
      version_t first = get_first_committed();

      if ((trim_to > 0) && trim_to > first)
        return ((trim_to - first) >= (version_t)g_conf->paxos_service_trim_min);
    }
    return true;
  }
  /**
   * Check if we should trim.
   *
   * We define this function here, because we assume that as long as we know of
   * a version to trim, we should trim. However, any implementation should feel
   * free to define its own version of this function if deemed necessary.
   *
   * @returns true if we should trim; false otherwise.
   */
  virtual bool service_should_trim() {
    update_trim();
    return (get_trim_to() > 0);
  }
  /**
   * Update our trim status. We do nothing here, because there is no
   * straightforward way to update the trim version, since that's service
   * specific. However, we do not force services to implement it, since there
   * a couple of services that do not trim anything at all, and we don't want
   * to shove this function down their throats if they are never going to use
   * it anyway.
   */
  virtual void update_trim() { }
  /**
   * Set the trim version variable to @p ver
   *
   * @param ver The version to trim to.
   */
  void set_trim_to(version_t ver) {
    trim_version = ver;
  }
  /**
   * Get the version we should trim to.
   *
   * @returns the version we should trim to; if we return zero, it should be
   *	      assumed that there's no version to trim to.
   */
  version_t get_trim_to() {
    return trim_version;
  }
  /**
   * @}
   */
  /**
   * @defgroup PaxosService_h_Stash_Full
   * @{
   */
  virtual bool should_stash_full();
  /**
   * Encode a full version on @p t
   *
   * @note We force every service to implement this function, since we strongly
   *	   desire the encoding of full versions.
   * @note Services that do not trim their state, will be bound to only create
   *	   one full version. Full version stashing is determined/controled by
   *	   trimming: we stash a version each time a trim is bound to erase the
   *	   latest full version.
   *
   * @param t Transaction on which the full version shall be encoded.
   */
  virtual void encode_full(MonitorDBStore::Transaction *t) = 0;
  /**
   * @}
   */

  
  /**
   * @defgroup PaxosService_h_store_funcs Back storage interface functions
   * @{
   */
  /**
   * @defgroup PaxosService_h_store_modify Wrapper function interface to access
   *					   the back store for modification
   *					   purposes
   * @{
   */
  void put_first_committed(MonitorDBStore::Transaction *t, version_t ver) {
    t->put(get_service_name(), first_committed_name, ver);
  }
  /**
   * Set the last committed version to @p ver
   *
   * @param t A transaction to which we add this put operation
   * @param ver The last committed version number being put
   */
  virtual void put_last_committed(MonitorDBStore::Transaction *t, version_t ver) {
    t->put(get_service_name(), last_committed_name, ver);

    /* We only need to do this once, and that is when we are about to make our
     * first proposal. There are some services that rely on first_committed
     * being set -- and it should! -- so we need to guarantee that it is,
     * specially because the services itself do not do it themselves. They do
     * rely on it, but they expect us to deal with it, and so we shall.
     */
    if (!get_first_committed())
      put_first_committed(t, ver);
  }

  /**
   * Put the contents of @p bl into version @p ver
   *
   * @param t A transaction to which we will add this put operation
   * @param ver The version to which we will add the value
   * @param bl A bufferlist containing the version's value
   */
  void put_version(MonitorDBStore::Transaction *t, version_t ver,
		   bufferlist& bl) {
    t->put(get_service_name(), ver, bl);
  }
  /**
   * Put the contents of @p bl into a full version key for this service, that
   * will be created with @p ver in mind.
   *
   * @param t The transaction to which we will add this put operation
   * @param ver A version number
   * @param bl A bufferlist containing the version's value
   */
  void put_version_full(MonitorDBStore::Transaction *t,
			version_t ver, bufferlist& bl) {
    string key = mon->store->combine_strings(full_prefix_name, ver);
    t->put(get_service_name(), key, bl);
  }
  /**
   * Put the version number in @p ver into the key pointing to the latest full
   * version of this service.
   *
   * @param t The transaction to which we will add this put operation
   * @param ver A version number
   */
  void put_version_latest_full(MonitorDBStore::Transaction *t, version_t ver) {
    string key = mon->store->combine_strings(full_prefix_name, full_latest_name);
    t->put(get_service_name(), key, ver);
  }


  /**
   * @}
   */

  /**
   * @defgroup PaxosService_h_version_cache Obtain cached versions for this
   *                                        service.
   * @{
   */
  /**
   * Get the first committed version
   *
   * @returns Our first committed version (that is available)
   */
  version_t get_first_committed() {
    return cached_first_committed;
  }

  /**
   * @}
   */

  /**
   * Get the contents of a given version @p ver
   *
   * @param ver The version being obtained
   * @param bl The bufferlist to be populated
   * @return 0 on success; <0 otherwise
   */
  int get_version(version_t ver, bufferlist& bl) {
    return mon->store->get(get_service_name(), ver, bl);
  }
  /**
   * Get the contents of a given full version of this service.
   *
   * @param ver A version number
   * @param bl The bufferlist to be populated
   * @returns 0 on success; <0 otherwise
   */
  int get_version_full(version_t ver, bufferlist& bl) {
    string key = mon->store->combine_strings(full_prefix_name, ver);
    return mon->store->get(get_service_name(), key, bl);
  }
  /**
   * Get the latest full version number
   *
   * @returns A version number
   */
  version_t get_version_latest_full() {
    string key = mon->store->combine_strings(full_prefix_name, full_latest_name);
    return mon->store->get(get_service_name(), key);
  }

};

#endif
