// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#ifndef CEPH_SMALL_ENCODING_H
#define CEPH_SMALL_ENCODING_H

#include "include/buffer.h"
#include "include/int_types.h"

// prefix int encoding
//
// encode the number of additional bytes in the low 3, 2, or 1 bits
// (depending on the variable size)
inline void small_encode_prefixint(uint64_t v, bufferlist& bl) {
  int bits = v ? (64 - clzll(v)) : 0;
  int bytes = (bits + 7 - 5) / 8;
  char buf[9];
  char *p = buf;
  *p++ = std::min(bytes, 7) | (v << 3);
  if (bytes) {
    *((uint64_t*)p) = v >> 5;   // fixme: little-endian only!
    if (bytes == 7) {
      bytes = 8;
    }
  }
  bl.append(buf, 1 + bytes);
}

inline void small_decode_prefixint(uint64_t& v, bufferlist::iterator& p) {
  uint8_t first;
  ::decode(first, p);
  int bytes = first & 7;
  if (bytes) {
    // fixme: little-endian only
    if (bytes == 7) {
      bytes = 8;
    }
    uint64_t t = 0;
    p.copy(bytes, (char*)&t);
    v = ((uint64_t)first >> 3) | (t << 5);
  } else {
    v = first >> 3;
  }
}

inline void small_encode_prefixint(uint32_t v, bufferlist& bl) {
  int bits = v ? ((sizeof(unsigned long)*8) - clzl((unsigned long)v)) : 0;
  int bytes = (bits + 7 - 6) / 8;
  char buf[9];
  char *p = buf;
  *p++ = std::min(bytes, 3) | (v << 2);
  if (bytes) {
    *((uint64_t*)p) = v >> 6;   // fixme: little-endian only!
    if (bytes == 3) {
      bytes = 4;
    }
  }
  bl.append(buf, 1 + bytes);
}

inline void small_decode_prefixint(uint32_t& v, bufferlist::iterator& p) {
  uint8_t first;
  ::decode(first, p);
  int bytes = first & 3;
  if (bytes) {
    if (bytes == 3) {
      bytes = 4;
    }
    // fixme: little-endian only
    uint32_t t = 0;
    p.copy(bytes, (char*)&t);
    v = ((uint32_t)first >> 2) | (t << 6);
  } else {
    v = first >> 2;
  }
}

inline void small_encode_prefixint(uint16_t v, bufferlist& bl) {
  int bits = v ? (32 - clz((unsigned)v)) : 0;
  int bytes = bits / 8;
  char buf[9];
  char *p = buf;
  *p++ = bytes | (v << 1);
  if (bytes) {
    *p++ = v >> 7;
  }
  bl.append(buf, 1 + bytes);
}

inline void small_decode_prefixint(uint16_t& v, bufferlist::iterator& p) {
  uint8_t first;
  ::decode(first, p);
  int bytes = first & 1;
  if (bytes) {
    uint8_t t;
    ::decode(t, p);
    v = ((unsigned)first >> 1) | ((unsigned)t << 7);
  } else {
    v = first >> 1;
  }
}


// varint encoding
//
// high bit of every byte indicates whether another byte follows.
template<typename T>
inline void small_encode_varint(T v, bufferlist& bl) {
  char buf[sizeof(T) + 2];
  char *p = buf;
  *p = v & 0x7f;
  v >>= 7;
  if (v) {
    *p++ |= 0x80;
    *p = v & 0x7f;
    v >>= 7;
    if (sizeof(v) > 1 && v) {
      *p++ |= 0x80;
      *p = v & 0x7f;
      v >>= 7;
      if (sizeof(v) > 2 && v) {
	*p++ |= 0x80;
	*p = v & 0x7f;
	v >>= 7;
	if (v) {
	  *p++ |= 0x80;
	  *p = v & 0x7f;
	  v >>= 7;
	  if (sizeof(v) > 4 && v) {
	    *p++ |= 0x80;
	    *p = v & 0x7f;
	    v >>= 7;
	    if (v) {
	      *p++ |= 0x80;
	      *p = v & 0x7f;
	      v >>= 7;
	      if (v) {
		*p++ |= 0x80;
		*p = v & 0x7f;
		v >>= 7;
		if (v) {
		  *p++ |= 0x80;
		  *p = v & 0x7f;
		}
	      }
	    }
	  }
	}
      }
    }
  }
  bl.append(buf, p + 1 - buf);
}

template<typename T>
inline void small_decode_varint(T& v, bufferlist::iterator& p)
{
  uint8_t byte;
  ::decode(byte, p);
  v = byte & 0x7f;
  int shift = 7;
  while (byte & 0x80) {
    ::decode(byte, p);
    v |= (T)(byte & 0x7f) << shift;
    shift += 7;
  }
}

// signed varint encoding
//
// low bit = 1 = negative, 0 = positive
// high bit of every byte indicates whether another byte follows.
inline void small_encode_signed_varint(int64_t v, bufferlist& bl) {
  if (v < 0) {
    v = (-v << 1) | 1;
  } else {
    v <<= 1;
  }
  small_encode_varint(v, bl);
}

template<typename T>
inline void small_decode_signed_varint(T& v, bufferlist::iterator& p)
{
  int64_t i;
  small_decode_varint(i, p);
  if (i & 1) {
    v = -(i >> 1);
  } else {
    v = i >> 1;
  }
}

// varint + lowz encoding
//
// first(low) 2 bits = how many low zero bits (nibbles)
// high bit of each byte = another byte follows
// (so, 5 bits data in first byte, 7 bits data thereafter)
inline void small_encode_varint_lowz(uint64_t v, bufferlist& bl) {
  int lowznib = v ? (ctz(v) / 4) : 0;
  if (lowznib > 3)
    lowznib = 3;
  v >>= lowznib * 4;
  v <<= 2;
  v |= lowznib;
  small_encode_varint(v, bl);
}

template<typename T>
inline void small_decode_varint_lowz(T& v, bufferlist::iterator& p)
{
  uint64_t i;
  small_decode_varint(i, p);
  int lowznib = (i & 3);
  i >>= 2;
  i <<= lowznib * 4;
  v = i;
}

// signed varint + lowz encoding
//
// first low bit = 1 for negative, 0 for positive
// next 2 bits = how many low zero bits (nibbles)
// high bit of each byte = another byte follows
// (so, 4 bits data in first byte, 7 bits data thereafter)
inline void small_encode_signed_varint_lowz(int64_t v, bufferlist& bl) {
  bool negative = false;
  if (v < 0) {
    v = -v;
    negative = true;
  }
  int lowznib = v ? (ctz(v) / 4) : 0;
  if (lowznib > 3)
    lowznib = 3;
  v >>= lowznib * 4;
  v <<= 3;
  v |= lowznib << 1;
  v |= (int)negative;
  small_encode_varint(v, bl);
}

template<typename T>
inline void small_decode_signed_varint_lowz(T& v, bufferlist::iterator& p)
{
  int64_t i;
  small_decode_varint(i, p);
  int lowznib = (i & 6) >> 1;
  if (i & 1) {
    i >>= 3;
    i <<= lowznib * 4;
    v = -i;
  } else {
    i >>= 3;
    i <<= lowznib * 4;
    v = i;
  }
}


// LBA
//
// first 1-3 bits = how many low zero bits
//     *0 = 12 (common 4 K alignment case)
//    *01 = 16
//   *011 = 20
//   *111 = byte
// then 28-30 bits of data
// then last bit = another byte follows
// high bit of each subsequent byte = another byte follows
inline void small_encode_lba(uint64_t v, bufferlist& bl) {
  int low_zero_nibbles = v ? (int)(ctz(v) / 4) : 0;
  int pos;
  uint32_t word;
  int t = low_zero_nibbles - 3;
  if (t < 0) {
    pos = 3;
    word = 0x7;
  } else if (t < 3) {
    v >>= (low_zero_nibbles * 4);
    pos = t + 1;
    word = (1 << t) - 1;
  } else {
    v >>= 20;
    pos = 3;
    word = 0x3;
  }
  word |= (v << pos) & 0x7fffffff;
  v >>= 31 - pos;
  if (!v) {
    ::encode(word, bl);
    return;
  }
  word |= 0x80000000;
  ::encode(word, bl);
  uint8_t byte = v & 0x7f;
  v >>= 7;
  while (v) {
    byte |= 0x80;
    ::encode(byte, bl);
    byte = (v & 0x7f);
    v >>= 7;
  }
  ::encode(byte, bl);
}

inline void small_decode_lba(uint64_t& v, bufferlist::iterator& p) {
  uint32_t word;
  ::decode(word, p);
  int shift;
  switch (word & 7) {
  case 0:
  case 2:
  case 4:
  case 6:
    v = (uint64_t)(word & 0x7ffffffe) << (12 - 1);
    shift = 12 + 30;
    break;
  case 1:
  case 5:
    v = (uint64_t)(word & 0x7ffffffc) << (16 - 2);
    shift = 16 + 29;
    break;
  case 3:
    v = (uint64_t)(word & 0x7ffffff8) << (20 - 3);
    shift = 20 + 28;
    break;
  case 7:
    v = (uint64_t)(word & 0x7ffffff8) >> 3;
    shift = 28;
  }
  uint8_t byte = word >> 24;
  while (byte & 0x80) {
    ::decode(byte, p);
    v |= (uint64_t)(byte & 0x7f) << shift;
    shift += 7;
  }
}


// short bufferptrs, bufferlists, strings
template<typename T>
inline void small_encode_buf_lowz(const T& bp, bufferlist& bl) {
  size_t l = bp.length();
  small_encode_varint_lowz(l, bl);
  bl.append(bp);
}
template<typename T>
inline void small_decode_buf_lowz(T& bp, bufferlist::iterator& p) {
  size_t l;
  small_decode_varint_lowz(l, p);
  p.copy(l, bp);
}

// STL containers

template<typename T>
inline void small_encode_obj(const std::vector<T>& v, bufferlist& bl) {
  size_t n = v.size();
  small_encode_varint(n, bl);
  for (auto p = v.cbegin(); p != v.cend(); ++p) {
    p->encode(bl);
  }
}
template<typename T>
inline void small_decode_obj(std::vector<T>& v, bufferlist::iterator& p) {
  size_t n;
  small_decode_varint(n, p);
  v.clear();
  while (n--) {
    v.push_back(T());
    v.back().decode(p);
  }
}

#endif
