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
 */

#include "bluestore_types.h"
#include "common/Formatter.h"
#include "include/stringify.h"

// bluestore_bdev_label_t

void bluestore_bdev_label_t::encode(bufferlist& bl) const
{
  // be slightly friendly to someone who looks at the device
  bl.append("bluestore block device\n");
  bl.append(stringify(osd_uuid));
  bl.append("\n");
  ENCODE_START(1, 1, bl);
  ::encode(osd_uuid, bl);
  ::encode(size, bl);
  ::encode(btime, bl);
  ::encode(description, bl);
  ENCODE_FINISH(bl);
}

void bluestore_bdev_label_t::decode(bufferlist::iterator& p)
{
  p.advance(60); // see above
  DECODE_START(1, p);
  ::decode(osd_uuid, p);
  ::decode(size, p);
  ::decode(btime, p);
  ::decode(description, p);
  DECODE_FINISH(p);
}

void bluestore_bdev_label_t::dump(Formatter *f) const
{
  f->dump_stream("osd_uuid") << osd_uuid;
  f->dump_unsigned("size", size);
  f->dump_stream("btime") << btime;
  f->dump_string("description", description);
}

void bluestore_bdev_label_t::generate_test_instances(
  list<bluestore_bdev_label_t*>& o)
{
  o.push_back(new bluestore_bdev_label_t);
  o.push_back(new bluestore_bdev_label_t);
  o.back()->size = 123;
  o.back()->btime = utime_t(4, 5);
  o.back()->description = "fakey";
}

ostream& operator<<(ostream& out, const bluestore_bdev_label_t& l)
{
  return out << "bdev(osd_uuid " << l.osd_uuid
	     << " size 0x" << std::hex << l.size << std::dec
	     << " btime " << l.btime
	     << " desc " << l.description << ")";
}

// cnode_t

void bluestore_cnode_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(bits, bl);
  ENCODE_FINISH(bl);
}

void bluestore_cnode_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(bits, p);
  DECODE_FINISH(p);
}

void bluestore_cnode_t::dump(Formatter *f) const
{
  f->dump_unsigned("bits", bits);
}

void bluestore_cnode_t::generate_test_instances(list<bluestore_cnode_t*>& o)
{
  o.push_back(new bluestore_cnode_t());
  o.push_back(new bluestore_cnode_t(0));
  o.push_back(new bluestore_cnode_t(123));
}

// bluestore_extent_t

string bluestore_extent_t::get_flags_string(unsigned flags)
{
  string s;
  if (flags & FLAG_SHARED) {
    if (s.length())
      s += '+';
    s += "shared";
  }
  return s;
}

void bluestore_extent_t::dump(Formatter *f) const
{
  f->dump_unsigned("offset", offset);
  f->dump_unsigned("length", length);
  f->dump_unsigned("flags", flags);
}

void bluestore_extent_t::generate_test_instances(list<bluestore_extent_t*>& o)
{
  o.push_back(new bluestore_extent_t());
  o.push_back(new bluestore_extent_t(123, 456));
  o.push_back(new bluestore_extent_t(789, 1024, 322));
}

ostream& operator<<(ostream& out, const bluestore_extent_t& e)
{
  out << e.offset << "~" << e.length;
  if (e.flags)
    out << ":" << bluestore_extent_t::get_flags_string(e.flags);
  return out;
}

// bluestore_extent_ref_map_t

void bluestore_extent_ref_map_t::_check() const
{
  uint64_t pos = 0;
  unsigned refs = 0;
  for (const auto &p : ref_map) {
    if (p.first < pos)
      assert(0 == "overlap");
    if (p.first == pos && p.second.refs == refs)
      assert(0 == "unmerged");
    pos = p.first + p.second.length;
    refs = p.second.refs;
  }
}

void bluestore_extent_ref_map_t::_maybe_merge_left(map<uint64_t,record_t>::iterator& p)
{
  if (p == ref_map.begin())
    return;
  auto q = p;
  --q;
  if (q->second.refs == p->second.refs &&
      q->first + q->second.length == p->first) {
    q->second.length += p->second.length;
    ref_map.erase(p);
    p = q;
  }
}

void bluestore_extent_ref_map_t::get(uint64_t offset, uint32_t length)
{
  map<uint64_t,record_t>::iterator p = ref_map.lower_bound(offset);
  if (p != ref_map.begin()) {
    --p;
    if (p->first + p->second.length <= offset) {
      ++p;
    }
  }
  while (length > 0) {
    if (p == ref_map.end()) {
      // nothing after offset; add the whole thing.
      p = ref_map.insert(
	map<uint64_t,record_t>::value_type(offset, record_t(length, 1))).first;
      break;
    }
    if (p->first > offset) {
      // gap
      uint64_t newlen = MIN(p->first - offset, length);
      p = ref_map.insert(
	map<uint64_t,record_t>::value_type(offset,
					   record_t(newlen, 1))).first;
      offset += newlen;
      length -= newlen;
      _maybe_merge_left(p);
      ++p;
      continue;
    }
    if (p->first < offset) {
      // split off the portion before offset
      assert(p->first + p->second.length > offset);
      uint64_t left = p->first + p->second.length - offset;
      p->second.length = offset - p->first;
      p = ref_map.insert(map<uint64_t,record_t>::value_type(
			   offset, record_t(left, p->second.refs))).first;
      // continue below
    }
    assert(p->first == offset);
    if (length < p->second.length) {
      ref_map.insert(make_pair(offset + length,
			       record_t(p->second.length - length,
					p->second.refs)));
      p->second.length = length;
      ++p->second.refs;
      break;
    }
    ++p->second.refs;
    offset += p->second.length;
    length -= p->second.length;
    _maybe_merge_left(p);
    ++p;
  }
  if (p != ref_map.end())
    _maybe_merge_left(p);
  _check();
}

void bluestore_extent_ref_map_t::put(
  uint64_t offset, uint32_t length,
  vector<bluestore_pextent_t> *release)
{
  map<uint64_t,record_t>::iterator p = ref_map.lower_bound(offset);
  if (p == ref_map.end() || p->first > offset) {
    if (p == ref_map.begin()) {
      assert(0 == "put on missing extent (nothing before)");
    }
    --p;
    if (p->first + p->second.length <= offset) {
      assert(0 == "put on missing extent (gap)");
    }
  }
  if (p->first < offset) {
    uint64_t left = p->first + p->second.length - offset;
    p->second.length = offset - p->first;
    p = ref_map.insert(map<uint64_t,record_t>::value_type(
			 offset, record_t(left, p->second.refs))).first;
  }
  while (length > 0) {
    assert(p->first == offset);
    if (length < p->second.length) {
      ref_map.insert(make_pair(offset + length,
			       record_t(p->second.length - length,
					p->second.refs)));
      if (p->second.refs > 1) {
	p->second.length = length;
	--p->second.refs;
	_maybe_merge_left(p);
      } else {
	if (release)
	  release->push_back(bluestore_pextent_t(p->first, length));
	ref_map.erase(p);
      }
      return;
    }
    offset += p->second.length;
    length -= p->second.length;
    if (p->second.refs > 1) {
      --p->second.refs;
      _maybe_merge_left(p);
      ++p;
    } else {
      if (release)
	release->push_back(bluestore_pextent_t(p->first, p->second.length));
      ref_map.erase(p++);
    }
  }
  if (p != ref_map.end())
    _maybe_merge_left(p);
  _check();
}

bool bluestore_extent_ref_map_t::contains(uint64_t offset, uint32_t length) const
{
  map<uint64_t,record_t>::const_iterator p = ref_map.lower_bound(offset);
  if (p == ref_map.end() || p->first > offset) {
    if (p == ref_map.begin()) {
      return false; // nothing before
    }
    --p;
    if (p->first + p->second.length <= offset) {
      return false; // gap
    }
  }
  while (length > 0) {
    if (p == ref_map.end())
      return false;
    if (p->first > offset)
      return false;
    if (p->first + p->second.length >= offset + length)
      return true;
    uint64_t overlap = p->first + p->second.length - offset;
    offset += overlap;
    length -= overlap;
    ++p;
  }
  return true;
}

bool bluestore_extent_ref_map_t::intersects(
  uint64_t offset,
  uint32_t length) const
{
  map<uint64_t,record_t>::const_iterator p = ref_map.lower_bound(offset);
  if (p != ref_map.begin()) {
    --p;
    if (p->first + p->second.length <= offset) {
      ++p;
    }
  }
  if (p == ref_map.end())
    return false;
  if (p->first >= offset + length)
    return false;
  return true;  // intersects p!
}

void bluestore_extent_ref_map_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(ref_map, bl);
  ENCODE_FINISH(bl);
}

void bluestore_extent_ref_map_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(ref_map, p);
  DECODE_FINISH(p);
}

void bluestore_extent_ref_map_t::dump(Formatter *f) const
{
  f->open_array_section("ref_map");
  for (auto& p : ref_map) {
    f->open_object_section("ref");
    f->dump_unsigned("offset", p.first);
    f->dump_unsigned("length", p.second.length);
    f->dump_unsigned("refs", p.second.refs);
    f->close_section();
  }
  f->close_section();
}

void bluestore_extent_ref_map_t::generate_test_instances(list<bluestore_extent_ref_map_t*>& o)
{
  o.push_back(new bluestore_extent_ref_map_t);
  o.push_back(new bluestore_extent_ref_map_t);
  o.back()->get(10, 10);
  o.back()->get(18, 22);
  o.back()->get(20, 20);
  o.back()->get(10, 25);
  o.back()->get(15, 20);
}

ostream& operator<<(ostream& out, const bluestore_extent_ref_map_t& m)
{
  out << "ref_map(";
  for (auto p = m.ref_map.begin(); p != m.ref_map.end(); ++p) {
    if (p != m.ref_map.begin())
      out << ",";
    out << std::hex << "0x" << p->first << "~0x" << p->second.length << std::dec
	<< "=" << p->second.refs;
  }
  out << ")";
  return out;
}

// bluestore_overlay_t

void bluestore_overlay_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(key, bl);
  ::encode(value_offset, bl);
  ::encode(length, bl);
  ENCODE_FINISH(bl);
}

void bluestore_overlay_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(key, p);
  ::decode(value_offset, p);
  ::decode(length, p);
  DECODE_FINISH(p);
}

void bluestore_overlay_t::dump(Formatter *f) const
{
  f->dump_unsigned("key", key);
  f->dump_unsigned("value_offset", value_offset);
  f->dump_unsigned("length", length);
}

void bluestore_overlay_t::generate_test_instances(list<bluestore_overlay_t*>& o)
{
  o.push_back(new bluestore_overlay_t());
  o.push_back(new bluestore_overlay_t(789, 1024, 1232232));
}

ostream& operator<<(ostream& out, const bluestore_overlay_t& o)
{
  out << "overlay(0x" << std::hex << o.value_offset << "~0x" << o.length
      << std::dec << " key " << o.key << ")";
  return out;
}


// bluestore_pextent_t

void bluestore_pextent_t::dump(Formatter *f) const
{
  f->dump_unsigned("offset", offset);
  f->dump_unsigned("length", length);
}

ostream& operator<<(ostream& out, const bluestore_pextent_t& o) {
  return out << "0x" << std::hex << o.offset << "~0x" << o.length << std::dec;
}

void bluestore_pextent_t::generate_test_instances(list<bluestore_pextent_t*>& ls)
{
  ls.push_back(new bluestore_pextent_t);
  ls.push_back(new bluestore_pextent_t(1, 2));
}

// bluestore_blob_t

string bluestore_blob_t::get_flags_string(unsigned flags)
{
  string s;
  if (flags & FLAG_MUTABLE) {
    if (s.length())
      s += '+';
    s += "mutable";
  }
  if (flags & FLAG_COMPRESSED) {
    if (s.length())
      s += '+';
    s += "compressed";
  }
  return s;
}

void bluestore_blob_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(extents, bl);
  ::encode(length, bl);
  ::encode(flags, bl);
  ::encode(csum_type, bl);
  ::encode(csum_block_order, bl);
  ::encode(ref_map, bl);
  ::encode(csum_data, bl);
  ENCODE_FINISH(bl);
}

void bluestore_blob_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(extents, p);
  ::decode(length, p);
  ::decode(flags, p);
  ::decode(csum_type, p);
  ::decode(csum_block_order, p);
  ::decode(ref_map, p);
  ::decode(csum_data, p);
  DECODE_FINISH(p);
}

void bluestore_blob_t::dump(Formatter *f) const
{
  f->open_array_section("extents");
  for (auto& p : extents) {
    f->dump_object("extent", p);
  }
  f->close_section();
  f->dump_unsigned("length", length);
  f->dump_unsigned("flags", flags);
  f->dump_unsigned("csum_type", csum_type);
  f->dump_unsigned("csum_block_order", csum_block_order);
  f->dump_object("ref_map", ref_map);
  f->open_array_section("csum_data");
  size_t n = get_csum_count();
  for (unsigned i = 0; i < n; ++i)
    f->dump_unsigned("csum", get_csum_item(i));
  f->close_section();
}

void bluestore_blob_t::generate_test_instances(list<bluestore_blob_t*>& ls)
{
  ls.push_back(new bluestore_blob_t);
  ls.push_back(new bluestore_blob_t(4096, 0));
  ls.push_back(new bluestore_blob_t(4096, bluestore_pextent_t(111, 222), 12));
  ls.push_back(new bluestore_blob_t(4096, bluestore_pextent_t(111, 222), 12));
  ls.back()->csum_type = CSUM_XXHASH32;
  ls.back()->csum_block_order = 16;
  ls.back()->csum_data = vector<char>{1, 2, 3, 4};  // one uint32_t
  ls.back()->ref_map.get(3, 5);
}

ostream& operator<<(ostream& out, const bluestore_blob_t& o)
{
  out << "blob(" << o.extents
      << " len 0x" << std::hex << o.length << std::dec;
  if (o.flags) {
    out << " " << o.get_flags_string();
  }
  if (o.csum_type) {
    out << " " << o.get_csum_type_string(o.csum_type)
	<< "/0x" << std::hex << (1ull << o.csum_block_order) << std::dec;
  }
  if (!o.ref_map.empty()) {
    out << " " << o.ref_map;
  }
  out << ")";
  return out;
}

// bluestore_lextent_t

string bluestore_lextent_t::get_flags_string(unsigned flags)
{
  string s;
  return s;
}

void bluestore_lextent_t::dump(Formatter *f) const
{
  f->dump_unsigned("blob", blob);
  f->dump_unsigned("offset", offset);
  f->dump_unsigned("length", length);
  f->dump_unsigned("flags", flags);
}

void bluestore_lextent_t::generate_test_instances(list<bluestore_lextent_t*>& ls)
{
  ls.push_back(new bluestore_lextent_t);
  ls.push_back(new bluestore_lextent_t(23232, 0, 4096, 0));
  ls.push_back(new bluestore_lextent_t(23232, 16384, 8192, 7));
}

ostream& operator<<(ostream& out, const bluestore_lextent_t& lb)
{
  out  << "0x" << std::hex << lb.offset << "~0x" << lb.length << std::dec
       << "->" << lb.blob;
  if (lb.flags)
    out << ":" << bluestore_lextent_t::get_flags_string(lb.flags);
  return out;
}


// bluestore_onode_t

void bluestore_onode_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(nid, bl);
  ::encode(size, bl);
  ::encode(attrs, bl);
  ::encode(blob_map, bl);
  ::encode(extent_map, bl);
  ::encode(overlay_map, bl);
  ::encode(overlay_refs, bl);
  ::encode(last_overlay_key, bl);
  ::encode(omap_head, bl);
  ::encode(expected_object_size, bl);
  ::encode(expected_write_size, bl);
  ENCODE_FINISH(bl);
}

void bluestore_onode_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(nid, p);
  ::decode(size, p);
  ::decode(attrs, p);
  ::decode(blob_map, p);
  ::decode(extent_map, p);
  ::decode(overlay_map, p);
  ::decode(overlay_refs, p);
  ::decode(last_overlay_key, p);
  ::decode(omap_head, p);
  ::decode(expected_object_size, p);
  ::decode(expected_write_size, p);
  DECODE_FINISH(p);
}

void bluestore_onode_t::dump(Formatter *f) const
{
  f->dump_unsigned("nid", nid);
  f->dump_unsigned("size", size);
  f->open_object_section("attrs");
  for (map<string,bufferptr>::const_iterator p = attrs.begin();
       p != attrs.end(); ++p) {
    f->open_object_section("attr");
    f->dump_string("name", p->first);
    f->dump_unsigned("len", p->second.length());
    f->close_section();
  }
  f->close_section();
  f->open_object_section("blob_map");
  for (const auto& p : blob_map) {
    f->open_object_section("blob");
    f->dump_unsigned("id", p.first);
    p.second.dump(f);
    f->close_section();
  }
  f->close_section();
  f->open_object_section("extent_map");
  for (const auto& p : extent_map) {
    f->open_object_section("extent");
    f->dump_unsigned("logical_offset", p.first);
    p.second.dump(f);
    f->close_section();
  }
  f->close_section();
  f->open_object_section("overlays");
  for (map<uint64_t,bluestore_overlay_t>::const_iterator p = overlay_map.begin();
       p != overlay_map.end(); ++p) {
    f->open_object_section("overlay");
    f->dump_unsigned("offset", p->first);
    p->second.dump(f);
    f->close_section();
  }
  f->close_section();
  f->open_array_section("overlay_refs");
  for (map<uint64_t,uint16_t>::const_iterator p = overlay_refs.begin();
       p != overlay_refs.end(); ++p) {
    f->open_object_section("overlay");
    f->dump_unsigned("offset", p->first);
    f->dump_unsigned("refs", p->second);
    f->close_section();
  }
  f->close_section();
  f->dump_unsigned("last_overlay_key", last_overlay_key);
  f->dump_unsigned("omap_head", omap_head);
  f->dump_unsigned("expected_object_size", expected_object_size);
  f->dump_unsigned("expected_write_size", expected_write_size);
}

void bluestore_onode_t::generate_test_instances(list<bluestore_onode_t*>& o)
{
  o.push_back(new bluestore_onode_t());
  // FIXME
}

int bluestore_onode_t::compress_extent_map()
{
  if (extent_map.empty())
    return 0;
  int removed = 0;
  auto p = extent_map.begin();
  auto n = p;
  for (++n; n != extent_map.end(); p = n++) {
    while (n != extent_map.end() &&
	   p->first + p->second.length == n->first &&
	   p->second.blob == n->second.blob &&
	   p->second.offset + p->second.length == n->second.offset) {
      p->second.length += n->second.length;
      extent_map.erase(n++);
      ++removed;
    }
  }
  return removed;
}

void bluestore_onode_t::punch_hole(
  uint64_t offset,
  uint64_t length,
  vector<bluestore_lextent_t> *deref)
{
  auto p = seek_lextent(offset);
  uint64_t end = offset + length;
  while (p != extent_map.end()) {
    if (p->first >= end) {
      break;
    }
    if (p->first < offset) {
      if (p->first + p->second.length > end) {
	// split and deref middle
	uint64_t front = offset - p->first;
	deref->emplace_back(
	  bluestore_lextent_t(
	    p->second.blob,
	    p->second.offset + front,
	    length,
	    p->second.flags));
	extent_map[end] = bluestore_lextent_t(
	  p->second.blob,
	  p->second.offset + front + length,
	  p->second.length - front - length,
	  p->second.flags);
	p->second.length = front;
	break;
      } else {
	// deref tail
	assert(p->first + p->second.length > offset); // else bug in find_lextent
	uint64_t keep = offset - p->first;
	deref->emplace_back(
	  bluestore_lextent_t(
	    p->second.blob,
	    p->second.offset + keep,
	    p->second.length - keep,
	    p->second.flags));
	p->second.length = keep;
	++p;
	continue;
      }
    }
    if (p->first + p->second.length <= end) {
      // deref whole lextent
      deref->push_back(p->second);
      extent_map.erase(p++);
      continue;
    }
    // deref head
    uint64_t keep = (p->first + p->second.length) - end;
    deref->emplace_back(
      bluestore_lextent_t(
	p->second.blob,
	p->second.offset,
	p->second.length - keep,
	p->second.flags));
    extent_map[end] = bluestore_lextent_t(
      p->second.blob,
      p->second.offset + p->second.length - keep,
      keep,
      p->second.flags);
    extent_map.erase(p++);
    break;
  }
}

// bluestore_wal_op_t

void bluestore_wal_op_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(op, bl);
  ::encode(extents, bl);
  ::encode(data, bl);
  ::encode(nid, bl);
  ::encode(overlays, bl);
  ::encode(removed_overlays, bl);
  ENCODE_FINISH(bl);
}

void bluestore_wal_op_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(op, p);
  ::decode(extents, p);
  ::decode(data, p);
  ::decode(nid, p);
  ::decode(overlays, p);
  ::decode(removed_overlays, p);
  DECODE_FINISH(p);
}

void bluestore_wal_op_t::dump(Formatter *f) const
{
  f->dump_unsigned("op", (int)op);
  f->dump_unsigned("data_len", data.length());
  f->open_array_section("extents");
  for (auto& e : extents) {
    f->dump_object("extent", e);
  }
  f->close_section();
  f->dump_unsigned("nid", nid);
  f->open_array_section("overlays");
  for (auto& o : overlays) {
    f->dump_object("overlay", o);
  }
  f->close_section();
  f->open_array_section("removed_overlays");
  for (auto key : removed_overlays) {
    f->dump_unsigned("key", key);
  }
  f->close_section();
}

void bluestore_wal_op_t::generate_test_instances(list<bluestore_wal_op_t*>& o)
{
  o.push_back(new bluestore_wal_op_t);
  o.push_back(new bluestore_wal_op_t);
  o.back()->op = OP_WRITE;
  o.back()->extents.push_back(bluestore_pextent_t(1, 2));
  o.back()->extents.push_back(bluestore_pextent_t(100, 5));
  o.back()->data.append("my data");
}

void bluestore_wal_transaction_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(seq, bl);
  ::encode(ops, bl);
  ::encode(released, bl);
  ENCODE_FINISH(bl);
}

void bluestore_wal_transaction_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(seq, p);
  ::decode(ops, p);
  ::decode(released, p);
  DECODE_FINISH(p);
}

void bluestore_wal_transaction_t::dump(Formatter *f) const
{
  f->dump_unsigned("seq", seq);
  f->open_array_section("ops");
  for (list<bluestore_wal_op_t>::const_iterator p = ops.begin(); p != ops.end(); ++p) {
    f->dump_object("op", *p);
  }
  f->close_section();

  f->open_array_section("released extents");
  for (interval_set<uint64_t>::const_iterator p = released.begin(); p != released.end(); ++p) {
    f->open_object_section("extent");
    f->dump_unsigned("offset", p.get_start());
    f->dump_unsigned("length", p.get_len());
    f->close_section();
  }
  f->close_section();
}

void bluestore_wal_transaction_t::generate_test_instances(list<bluestore_wal_transaction_t*>& o)
{
  o.push_back(new bluestore_wal_transaction_t());
  o.push_back(new bluestore_wal_transaction_t());
  o.back()->seq = 123;
  o.back()->ops.push_back(bluestore_wal_op_t());
  o.back()->ops.push_back(bluestore_wal_op_t());
  o.back()->ops.back().op = bluestore_wal_op_t::OP_WRITE;
  o.back()->ops.back().extents.push_back(bluestore_pextent_t(1,7));
  o.back()->ops.back().data.append("foodata");
}

void bluestore_compression_header_t::encode(bufferlist& bl) const
{
  ENCODE_START(1, 1, bl);
  ::encode(type, bl);
  ENCODE_FINISH(bl);
}

void bluestore_compression_header_t::decode(bufferlist::iterator& p)
{
  DECODE_START(1, p);
  ::decode(type, p);
  DECODE_FINISH(p);
}

void bluestore_compression_header_t::dump(Formatter *f) const
{
  f->dump_string("type", type);
}

void bluestore_compression_header_t::generate_test_instances(
  list<bluestore_compression_header_t*>& o)
{
  o.push_back(new bluestore_compression_header_t);
  o.push_back(new bluestore_compression_header_t("some_header"));
}
