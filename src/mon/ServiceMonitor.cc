// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "ServiceMonitor.h"
#include "mon/OSDMonitor.h"
#include "mon/PGMap.h"
#include "mon/PGMonitor.h"
#include "messages/MGetPoolStats.h"
#include "messages/MGetPoolStatsReply.h"
#include "messages/MMonMgrReport.h"
#include "messages/MStatfs.h"
#include "messages/MStatfsReply.h"

#define dout_subsys ceph_subsys_mon
#undef dout_prefix
#define dout_prefix _prefix(_dout, mon)
static ostream& _prefix(std::ostream *_dout, Monitor *mon) {
  return *_dout << "mon." << mon->name << "@" << mon->rank
		<< "(" << mon->get_state_name()
		<< ").servicemap ";
}

ServiceMonitor::ServiceMonitor(Monitor *mn, Paxos *p, const string& service_name)
  : PaxosService(mn, p, service_name)
{
}

ServiceMonitor::~ServiceMonitor() = default;

void ServiceMonitor::create_initial()
{
  dout(10) << dendl;
  pending_map.epoch = 1;
}

void ServiceMonitor::update_from_paxos(bool *need_bootstrap)
{
  version_t version = get_last_committed();
  dout(10) << " " << version << dendl;
  bufferlist bl;
  get_version(version, bl);
  if (version) {
    assert(bl.length());
    auto p = bl.begin();
    ::decode(map, p);
    assert(map.epoch == version);
  }
}

void ServiceMonitor::create_pending()
{
  dout(10) << " " << (map.epoch + 1) << dendl;
  pending_map = map;
  pending_map.epoch++;
}

void ServiceMonitor::encode_pending(MonitorDBStore::TransactionRef t)
{
  dout(10) << " " << pending_map.epoch << dendl;
  bufferlist bl;
  ::encode(pending_map, bl, mon->get_quorum_con_features());
  put_version(t, pending_map.epoch, bl);
  put_last_committed(t, pending_map.epoch);
}

version_t ServiceMonitor::get_trim_to()
{
  // we don't actually need *any* old states, but keep a few.
  if (map.epoch > 5) {
    return map.epoch - 5;
  }
  return 0;
}

void ServiceMonitor::on_active()
{
}

void ServiceMonitor::get_health(list<pair<health_status_t,string> >& summary,
				list<pair<health_status_t,string> > *detail,
				CephContext *cct) const
{
}

void ServiceMonitor::tick()
{
}

void ServiceMonitor::print_summary(Formatter *f, std::ostream *ss) const
{
  *ss << "e" << map.epoch << " " << map.services.size() << " services";
}

bool ServiceMonitor::preprocess_query(MonOpRequestRef op)
{
  auto m = static_cast<PaxosServiceMessage*>(op->get_req());
  switch (m->get_type()) {
  case MSG_MON_COMMAND:
    return preprocess_command(op);
  default:
    mon->no_reply(op);
    derr << "Unhandled message type " << m->get_type() << dendl;
    return true;
  }
}

bool ServiceMonitor::prepare_update(MonOpRequestRef op)
{
  auto m = static_cast<PaxosServiceMessage*>(op->get_req());
  switch (m->get_type()) {
  case MSG_MON_COMMAND:
    return prepare_command(op);
  default:
    mon->no_reply(op);
    derr << "Unhandled message type " << m->get_type() << dendl;
    return true;
  }
}

bool ServiceMonitor::preprocess_command(MonOpRequestRef op)
{
  return false;
}

bool ServiceMonitor::prepare_command(MonOpRequestRef op)
{
  return false;
}
