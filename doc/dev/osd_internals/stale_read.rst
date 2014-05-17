Preventing Stale Reads
======================

We write synchronously to all replicas before sending an ack to the
client, which ensures that we do not introduce potential inconsistency
in the write path.  However, we only read from one replica, and the
client will use whatever OSDMap is has to identify which OSD to read
from.  In most cases, this is fine: either the client map is correct,
or the OSD that we think is the primary for the object knows that it
is not.

They key is to ensure that this is *always* true.  In particular, we need to
ensure that an OSD that is fenced off from its peers and has not learned about
a map update does not continue to service read requests from similarly stale
clients at any point after which a new primary may have been allowed to make
a write.

We accomplish this via a mechanism that works much like a read lease.
Each pool may have a ``read_lease_interval`` property which defines
how long this is, although by default we simply set it to
``osd_pool_readable_min_ratio`` (default: 2) times the
``osd_heartbeat_interval``.  (The read lease interval should always be
more than the heartbeat interval or else the lease will expire and
block IO every ping inteval.)

These leases are tied to the ping interval because we use the existing
MOSDPing messages to renew them on a per-OSD-pair basis instead of
sending explicit lease renewals for every PG in the system.

The read interval renews when the primary OSD *sends* the MOSDPing message.

Replicas calculate bound on the monotonic clock differential between
the primary and themselves and establish a *lower bound* on when the
lease will expire, and are only readable (in the case that
read-from-replica is allowed!) until then.  The replica *also*
calculates an *upper bound* on how long any other member of the acting
set may be readable, which e.g., might be something like the ping
receive time plus the read interval.

The primary is somewhat more conservative: it is only readable after
it knows that every member of the acting set has a suitable upper
bound, which means that it's lease extends from the time the last ping
message *that every replica has acked* was originally sent.  The
primary's upper bound, on the other hand, is the time the last ping
was sent plus the read interval, since replicas may be readable past
that point.




The interval timer is implicitly renewed by the primary OSD for an
interval by the OSDPing heartbeats.

Before a new primary OSD in a subsequent interval is allowed to
service writes, it must have either:

 * be certain that all OSDs in the prior interval(s) know that the
   past interval has concluded (usually as a side-effect of probing
   them during peering), or
 * be certain that the prior interval's read_interval window has
   passed as the prior OSDs are no longer servicing reads.

The key piece of information that the new primary needs is an upper
bound on when that time period has completed.

Because we do not want to be concerned about time sync issues, we
express the end of that interval in terms of "readable seconds
remaining" whenever exchanging messages over the wire, and in terms of
a timestamp when represented locally.  (We assume that any clock
jitter on the local node is not significant.)


On handle ping
-------

* note time ("last_reply")

On handle ping reply
-------------

* on ack, for that peer, note our send time ("last acked ping")

On read
-------

* readable_until = MIN(last_acked_ping for all peers) + read_interval
* defer read if now >= readable_until.

When sending pg_notify_t
------------------------

* include last_reply for primary + read_interval - now (this is an
  upper bound on readable time remaining)

During peering
--------------

* note peer_readable_until value for all notifies (now + readable time
  remaining)
* at activate, for all prior interval members that are in
  PriorSet.down, add to map osd -> MAX(maybe_rw interval end epoch)
  (prior_readers)
* if the resulting map is non-empty, delay activate until
  MAX(readable_until) for each time whose consumed_epoch is unknown or
  <= the prior_readers epoch


OSD incarnations
======

Generally speaking, if an OSD is down, the goal is to know that they
know or are in fact dead.

If an OSD is wrongly marked down, we want to share with our peers the
consumed_epoch so that we know the PGs got the map and will no longer
service reads.

If an OSD restarts, we know the old instance must be dead because of
the exclusive locking on the actual osd data store.


Notes
=====

The good news in all of this is that in the most common case, we are
marking a peer OSD down because it is failing to respond to
heartbeats.  By the time we do mark it down, the peers's readable
intervals will have expired and there will be no further delay in
peering.

The place where it generally *will* cause an additional delay is when
we manually mark an OSD down (ceph osd down NNN).  In that case, we
need to make the downed OSD send a few additional messages to its old
peers letting them know that it knows about it.  Likewise, the old
peers should send messages to the downed OSD telling it the news.

 XXX: specifically detail the ping exchanges


TODO
====

#. add heartbeat infrastructure so that we can calculate the
   readable_until value for an active pg
#. suspend ops if we have passed the readable_until time
#. resume ops if we get the relevant heartbeats
#. share readable_until values along with notify
#. inherit previous intervals' readable_until on pg activation

#. stress test tool




    P     R
    |     |
p1a o     |     first exchange only...
    |\    |
    | \   |
    |  \  |
    |   \ |
    |    \|
    |     o r1 -> r: ru    = -
    |    /|          ru_ub = r1 + interval
    |   / |
    |  /  |
    | /   |
    |/    |
p1b o     |  -> p: ru=p1a + interval, ru_ub=ru
    |     |
    |     |
    |     |
p2a o     |   for subsequent exchanges, use a mono_clock delta from prior rounds
    |\    |
    | \   |
    |  \  |
    |   \ |
    |    \|
    |     o r2 -> r: ru=r2 + delta_ub + interval
    |    /|          ru_ub=p2a + delta_ub + interval
    |   / |
    |  /  |
    | /   |
    |/    |
p2b o     |  -> p: ru    = p2a + interval
    |     |        ru_ub = last_ping_sent + interval
