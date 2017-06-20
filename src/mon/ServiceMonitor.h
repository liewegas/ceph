// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "include/Context.h"
#include "PaxosService.h"
#include "mon/ServiceMap.h"

class MonPGStatService;
class MgrPGStatService;

class ServiceMonitor : public PaxosService {
  // live version
  ServiceMap map;
  ServiceMap pending_map;

public:
  ServiceMonitor(Monitor *mn, Paxos *p, const string& service_name);
  ~ServiceMonitor() override;

  void init() override {}
  void on_shutdown() override {}

  void create_initial() override;
  void update_from_paxos(bool *need_bootstrap) override;
  void create_pending() override;
  void encode_pending(MonitorDBStore::TransactionRef t) override;
  version_t get_trim_to() override;

  bool preprocess_query(MonOpRequestRef op) override;
  bool prepare_update(MonOpRequestRef op) override;

  void encode_full(MonitorDBStore::TransactionRef t) override { }

  bool preprocess_command(MonOpRequestRef op);
  bool prepare_command(MonOpRequestRef op);

  void check_sub(Subscription *sub);
  void check_subs();
  void send_digests();

  void on_active() override;
  void get_health(list<pair<health_status_t,string> >& summary,
		  list<pair<health_status_t,string> > *detail,
		  CephContext *cct) const override;
  void tick() override;

  void print_summary(Formatter *f, std::ostream *ss) const;

  friend class C_Updated;
};
