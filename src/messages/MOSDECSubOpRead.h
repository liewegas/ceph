// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank Storage, Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef MOSDECSUBOPREAD_H
#define MOSDECSUBOPREAD_H

#include "MOSDFastDispatchOp.h"
#include "osd/ECMsgTypes.h"

class MOSDECSubOpRead : public MOSDFastDispatchOp {
  static const int HEAD_VERSION = 3;
  static const int COMPAT_VERSION = 1;

public:
  spg_t pgid;
  epoch_t map_epoch;
  ECSubRead op;

  int get_cost() const {
    return 0;
  }
  epoch_t get_map_epoch() const override {
    return map_epoch;
  }
  spg_t get_spg() const override {
    return pgid;
  }

  MOSDECSubOpRead()
    : MOSDFastDispatchOp(MSG_OSD_EC_READ, HEAD_VERSION, COMPAT_VERSION)
    {}

  virtual void decode_payload() {
    bufferlist::iterator p = payload.begin();
    ::decode(pgid, p);
    ::decode(map_epoch, p);
    ::decode(op, p);
    if (header.version >= 3) {
      decode_trace(p);
    }
  }

  virtual void encode_payload(uint64_t features) {
    ::encode(pgid, payload);
    ::encode(map_epoch, payload);
    ::encode(op, payload, features);
    encode_trace(payload, features);
  }

  const char *get_type_name() const { return "MOSDECSubOpRead"; }

  void print(ostream& out) const {
    out << "MOSDECSubOpRead(" << pgid
	<< " " << map_epoch
	<< " " << op;
    out << ")";
  }
};

#endif
