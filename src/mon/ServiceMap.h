// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include <string>
#include <map>
#include <list>

#include "include/utime.h"
#include "include/buffer.h"
#include "msg/msg_types.h"

namespace ceph {
  class Formatter;
}

struct ServiceMap {
  struct Daemon {
    std::string name;
    uint64_t gid = 0;
    entity_addr_t addr;
    epoch_t start_epoch = 0;   ///< epoch first registered
    utime_t start_stamp;       ///< timestamp daemon started/registered
    std::map<std::string,std::string> metadata;  ///< static metadata
    utime_t status_stamp;         ///< timestamp of last status update
    std::map<std::string,std::string> status;    ///< static metadata

    void encode(bufferlist& bl, uint64_t features) const;
    void decode(bufferlist::iterator& p);
    void dump(Formatter *f) const;
    static void generate_test_instances(std::list<Daemon*>& ls);
  };

  struct Service {
    std::string name;
    map<std::string,Daemon> daemons;

    void encode(bufferlist& bl, uint64_t features) const;
    void decode(bufferlist::iterator& p);
    void dump(Formatter *f) const;
    static void generate_test_instances(std::list<Service*>& ls);
  };

  epoch_t epoch = 0;
  utime_t modified;
  map<std::string,Service> services;

  void encode(bufferlist& bl, uint64_t features) const;
  void decode(bufferlist::iterator& p);
  void dump(Formatter *f) const;
  static void generate_test_instances(std::list<ServiceMap*>& ls);
};
WRITE_CLASS_ENCODER_FEATURES(ServiceMap)
WRITE_CLASS_ENCODER_FEATURES(ServiceMap::Service)
WRITE_CLASS_ENCODER_FEATURES(ServiceMap::Daemon)
