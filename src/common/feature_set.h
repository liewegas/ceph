// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 * @author Sage Weil <sage@newdream.net>
 */

#ifndef CEPH_FEATURESET_H
#define CEPH_FEATURESET_H

#include <ostream>
#include <list>

#include "include/assert.h"
#include "include/encoding.h"

namespace ceph {

  class Formatter;

  class feature_set_t {
    uint64_t features;

  public:
    feature_set_t() : features(0) {}

    /// initialize the set with a 0-terminated array of features
    feature_set_t(int *features) {
      assert(features);
      for (int *p = features; *p; p++)
	add(*p);
    }

    feature_set_t(const feature_set_t &o) : features(o.features) {}
    const feature_set_t& operator=(const feature_set_t& o) {
      features = o.features;
      return *this;
    }

    feature_set_t operator|(const feature_set_t& o) const {
      feature_set_t ret;
      ret.features = features | o.features;
      return ret;
    }
    void operator|=(const feature_set_t& o) {
      features |= o.features;
    }

    feature_set_t operator&(const feature_set_t& o) const {
      feature_set_t ret;
      ret.features = features & o.features;
      return ret;
    }
    void operator&=(const feature_set_t& o) {
      features &= o.features;
    }

    void print(std::ostream& out) const {
      out << std::hex << "0x" << features << std::dec;
    }

    operator bool() const {
      return features != 0;
    }

    bool contains(int fnum) const {
      return features & (1 << fnum);
    }
    void add(int fnum) {
      features |= (1 << fnum);
    }
    void remove(int fnum) {
      features &= ~(1 << fnum);
    }

    void encode(bufferlist& bl) const;
    void decode(bufferlist::iterator& p);
    void dump(Formatter *f) const;
    static void generate_test_instances(std::list<feature_set_t*>& o);
  };

  std::ostream& operator<<(std::ostream& out, const feature_set_t& o) {
    o.print(out);
    return out;
  }

}; // namespace ceph
WRITE_CLASS_ENCODER(feature_set_t);


#endif
