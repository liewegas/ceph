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

#include "common/feature_set.h"
#include "common/Formatter.h"

namespace ceph {

  void feature_set_t::encode(bufferlist& bl) const
  {
    ::encode(features, bl);
  }

  void feature_set_t::decode(bufferlist::iterator& p)
  {
    ::decode(features, p);
  }

  void feature_set_t::dump(Formatter *f) const {
    f->dump_unsigned("features", features);
  }

  void feature_set_t::generate_test_instances(std::list<feature_set_t*>& o)
  {
    o.push_back(new feature_set_t());
    o.push_back(new feature_set_t());
    o.back()->add(0);
    o.back()->add(13);
    o.back()->add(63);
  }

};
