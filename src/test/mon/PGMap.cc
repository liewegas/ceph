// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Inktank <info@inktank.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#include "mon/PGMap.h"
#include "gtest/gtest.h"

#include "include/stringify.h"


namespace {
  class CheckTextTable : public TextTable {
  public:
    CheckTextTable(bool verbose) {
      for (int i = 0; i < 4; i++) {
        define_column("", TextTable::LEFT, TextTable::LEFT);
      }
      if (verbose) {
        for (int i = 0; i < 6; i++) {
          define_column("", TextTable::LEFT, TextTable::LEFT);
        }
      }
    }
    const string& get(unsigned r, unsigned c) const {
      assert(r < row.size());
      assert(c < row[r].size());
      return row[r][c];
    }
  };

  // copied from PGMap.cc
  string percentify(float a) {
    stringstream ss;
    if (a < 0.01)
      ss << "0";
    else
      ss << std::fixed << std::setprecision(2) << a;
    return ss.str();
  }
}

// dump_object_stat_sum() is called by "ceph df" command
// with table, without formatter, verbose = true, not empty, avail > 0
TEST(pgmap, dump_object_stat_sum_0)
{
  bool verbose = true;
  CheckTextTable tbl(verbose);
  pool_stat_t pool_stat;
  object_stat_sum_t& sum = pool_stat.stats.sum;
  sum.num_bytes = 42 * 1024 * 1024;
  sum.num_objects = 42;
  sum.num_objects_degraded = 13; // there are 13 missings + not_yet_backfilled
  sum.num_objects_dirty = 2;
  sum.num_rd = 100;
  sum.num_rd_kb = 123;
  sum.num_wr = 101;
  sum.num_wr_kb = 321;    
  store_statfs_t &statfs = pool_stat.store_stats;
  statfs.stored = 42 * 1024 * 1024 - 5;
  statfs.allocated = 42 * 1024 * 1024 * 2;
  statfs.compressed_allocated = 4334;
  statfs.compressed_original = 1213;

  sum.calc_copies(3); // assuming we have 3 copies for each obj
  // nominal amount of space available for new objects in this pool
  uint64_t avail = 2016 * 1024 * 1024;
  pg_pool_t pool;
  pool.quota_max_objects = 2000;
  pool.quota_max_bytes = 2000 * 1024 * 1024;
  pool.size = 2;
  pool.type = pg_pool_t::TYPE_REPLICATED;
  PGMap::dump_object_stat_sum(tbl, nullptr, pool_stat, avail,
                                  pool.get_size(), verbose, &pool);  
  float copies_rate =
    (static_cast<float>(sum.num_object_copies - sum.num_objects_degraded) /
      sum.num_object_copies) * pool.get_size();
  float used_percent = (float)statfs.allocated /
    (statfs.allocated + avail) * 100;
  uint64_t stored = statfs.stored / copies_rate;

  unsigned col = 0;
  ASSERT_EQ(stringify(si_t(statfs.allocated)), tbl.get(0, col++));
  ASSERT_EQ(percentify(used_percent), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(avail/copies_rate)), tbl.get(0, col++));
  ASSERT_EQ(stringify(sum.num_objects), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(sum.num_objects_dirty)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(sum.num_rd)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(sum.num_wr)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(stored)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(statfs.compressed_allocated)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(statfs.compressed_original)), tbl.get(0, col++));
}

// with table, without formatter, verbose = true, empty, avail > 0
TEST(pgmap, dump_object_stat_sum_1)
{
  bool verbose = true;
  CheckTextTable tbl(verbose);
  pool_stat_t pool_stat;
  object_stat_sum_t& sum = pool_stat.stats.sum; // zero by default
  ASSERT_TRUE(sum.is_zero());
  // nominal amount of space available for new objects in this pool
  uint64_t avail = 2016 * 1024 * 1024;
  pg_pool_t pool;
  pool.quota_max_objects = 2000;
  pool.quota_max_bytes = 2000 * 1024 * 1024;
  pool.size = 2;
  pool.type = pg_pool_t::TYPE_REPLICATED;
  PGMap::dump_object_stat_sum(tbl, nullptr, pool_stat, avail,
                                  pool.get_size(), verbose, &pool);  
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, 0));
  unsigned col = 0;
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, col++));
  ASSERT_EQ(percentify(0), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(avail/pool.size)), tbl.get(0, col++));
  ASSERT_EQ(stringify(0), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, col++));
}

// with table, without formatter, verbose = false, empty, avail = 0
TEST(pgmap, dump_object_stat_sum_2)
{
  bool verbose = false;
  CheckTextTable tbl(verbose);
  pool_stat_t pool_stat;
  object_stat_sum_t& sum = pool_stat.stats.sum; // zero by default
  ASSERT_TRUE(sum.is_zero());
  // nominal amount of space available for new objects in this pool
  uint64_t avail = 0;
  pg_pool_t pool;
  pool.quota_max_objects = 2000;
  pool.quota_max_bytes = 2000 * 1024 * 1024;
  pool.size = 2;
  pool.type = pg_pool_t::TYPE_REPLICATED;

  PGMap::dump_object_stat_sum(tbl, nullptr, pool_stat, avail,
                                  pool.get_size(), verbose, &pool);  
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, 0));
  unsigned col = 0;
  ASSERT_EQ(stringify(si_t(0)), tbl.get(0, col++));
  ASSERT_EQ(percentify(0), tbl.get(0, col++));
  ASSERT_EQ(stringify(si_t(avail/pool.size)), tbl.get(0, col++));
  ASSERT_EQ(stringify(0), tbl.get(0, col++));
}
