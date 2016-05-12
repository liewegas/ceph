===================
BlueStore Internals
===================


Small write strategies
----------------------

* *Normal*: The normal write path writes to unused space, waits for IO to flush, then commits the metadata.

  - write to new blob
  - kv commit

* *A*: Vanilla WAL overwrite: commit intent to overwrite, then overwrite async. This matches legacy bluestore.

  - kv commit
  - wal overwrite

* *B*: Do read up-front to complete a full csum or comp block, then (wal) overwrite.

  - read (some surrounding data)
  - kv commit
  - wal overwrite (of larger extent)

* *C*: Vanilla WAL read/modify/write (like legacy bluestore).

  - kv commit
  - wal read/modify/write

* *D*: Fragment lextent space by writing small piece of data into a piecemeal blob (that collects random, noncontiguous bits of data
  we need to write).

  - write to a piecemeal blob (min_alloc_size or larger, but we use just one block of it)
  - kv commit

* *E*: Copy-on-write wal event: read from location A, combine with new write, write to location B.

  - kv commit
  - wal read from immutable blob A, modify, write to blob B

* *F*: Copy-on-write wal event: read from location A, combine with new write, write to location B.  Update csum/comp metadata.

  - kv commit
  - wal read from immutable blob A, verify csum, modify, [csum and/or compress+allocate,] write to blob B
  - kv update csum/comp/alloc metadata

+----------------------+--------+--------------+-------------+--------------+---------------+
|                      | raw    | raw (cached) | csum (4 KB) | csum (16 KB) | comp (128 KB) |
+----------------------+--------+--------------+-------------+--------------+---------------+
| 4 KB overwrite       | A      | A            | A           | B            | B or D        |
+----------------------+--------+--------------+-------------+--------------+---------------+
| 100 byte overwrite   | C      | A            | B           | B            | B or D        |
+----------------------+--------+--------------+-------------+--------------+---------------+
| 100 byte append      | C      | A            | B           | B            | B or D        |
+----------------------+--------+--------------+-------------+--------------+---------------+
+----------------------+--------+--------------+-------------+--------------+---------------+
| 4 KB clone overwrite | D or E |              | D or F      | D or F       | D or F        |
+----------------------+--------+--------------+-------------+--------------+---------------+
