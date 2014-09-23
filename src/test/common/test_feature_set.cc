// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Red Hat
 *
 * LGPL2.1 (see COPYING-LGPL2.1) or later
 */

#include <iostream>
#include <string>
#include <gtest/gtest.h>

#include "include/stringify.h"
#include "common/feature_set.h"

TEST(FeatureSet, Add) {
  feature_set_t fs;
  fs.add(1);
  fs.add(10);
  ASSERT_EQ(std::string("0x402"), stringify(fs));
  fs.add(10);
  ASSERT_EQ(std::string("0x402"), stringify(fs));
}

TEST(FeatureSet, Remove) {
  feature_set_t fs;
  fs.add(1);
  fs.add(7);
  fs.add(10);
  ASSERT_EQ(std::string("0x482"), stringify(fs));
  fs.remove(1);
  ASSERT_EQ(std::string("0x480"), stringify(fs));
  fs.remove(10);
  ASSERT_EQ(std::string("0x80"), stringify(fs));
  fs.remove(7);
  ASSERT_EQ(std::string("0x0"), stringify(fs));
  fs.remove(7);
  ASSERT_EQ(std::string("0x0"), stringify(fs));
}

TEST(FeatureSet, Union) {
  feature_set_t a, b, c;
  a.add(1);
  b.add(2);
  b.add(3);
  c = a | b;
  ASSERT_EQ(std::string("0xe"), stringify(c));
  feature_set_t d;
  d.add(4);
  c |= d;
  ASSERT_EQ(std::string("0x1e"), stringify(c));
}

TEST(FeatureSet, Intersection) {
  feature_set_t a, b, c, d;
  a.add(0);
  a.add(1);
  b.add(1);
  b.add(2);
  c = a & b;
  ASSERT_EQ(std::string("0x2"), stringify(c));
  c.add(2);
  ASSERT_EQ(std::string("0x6"), stringify(c));
  d.add(1);
  c &= d;
  ASSERT_EQ(std::string("0x2"), stringify(c));
}
