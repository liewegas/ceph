balancer plugin
================

Typically, if you say, the system is 70% filled, then, all the OSDs,
should be 70% filled (roughly). It shouldn’t be the case that, a particular
OSD is 90% filled whereas another one is 20% filled. If such a case exist,
then we say there is an uneven distribution. Uneven distribution may arise
out of a number of reasons, including improper distribution of PGs among the
OSDs. Even if the PGs, were balanced properly, with respect to the weights
of the OSDs, there is still chance of uneven distribution, because the number
of objects assigned to different PGs are not in required proportion. And even
if they were, the sizes of each object might vary.

The balancer plugin will automatically optimize the pg (or objects or byte-level)
distribution.  One needs to just turn it on and it will slowly and continuously
optimize the layout without the user having to think about it. In this case, optimization
means, changing the weights of the devices (OSDs), so that the right amount of PGs, get
mapped to each device, with respect to their relative weights. If one wanted to attain
byte-level even-distribution, they would have to change the key (``ceph balancer key``)
to `byte`. It is however, suggested that the key be set to `auto` mode, with weights for
each parameter.

While it is recommended that the balancer module be best left to run auto-magically,
it might interest certain users to turn off the automatic optimization, and instead
create optimization plans themselves, and execute them, whenever they feel the need
to do so. While there is no fixed way to determine when to execute the rebalance
plan, the *score* of the system on each parameter and the average score (can be
viewed using ``balancer eval``) will give a general idea as to how uneven the
current distribution is. The lower the score, the better the distribution,
with 0 meaning perfect distribution, and 1 meaning the average over-utilized device
is infinitely more weighted than the average utilization of all devices.

As a rule of thumb, if score is 0.08, then the average over-utilized device is 10%
over-weighted compared to the average utilization of all OSDs, and a score of 0.68
implies 100% more weighted. The scoring is non-linear, to signify that presence of
highly over-utilizaed devices is a pretty bad situation.

Working Process and Performance
-------------------------------

At each instance of optimization, balancer module creates a `Plan`. Then it uses the
CRUSH to identify the various OSDs in a pool and calculate the distribution of `pg`,
`bytes` and `objects`, from the OSDMap. Then it calculates, how the target distribution
of each of the parameter(`pg`, `bytes` and `objects`). Using an iterative gradient
descent algorithm it tries to calculate the new weights. In each step of iteration,
the module checks that the process has converged to the new-weights or not, by calculating
their expected distribution, should the weights get implemented. If the process converged,
or the maximum number of iterations were made, then the best-weights during the entire
process is recorded in the plan. The module then sends a `Mon` command, to actually
execute the new-weights, and then remove the plan from the memory. If the balancer is
set on automagic mode, this entire process, gets repeated until automagic mode is turned off.
There is a single threshold, max_misplaced, that controls what fraction of the PGs/objects
can be misplaced at once. Default is 3%. However, with automagic mode, one should expect
continous movement of data across the network, almost all the time. Again, the amount of
data that moves at any given time, can be limited using `max_misplaced` parameter.

The balancer module works with multiple roots, device classes, multiple takes in the CRUSH rules.
There is some restrictions, though, depending on the mode. Notably, the crush-compat only
has a single set of weights to adjust, so it can't do much if there are multiple hierarchies
being balanced that overlap over any of the same devices.

Before we can execute any new-weights, we model the size of each pg so that we can
tell how things change when we move pgs. It uses the pg stats (obtained from using
a private copy of the OSDMap), but that is an incomplete solution because we don't
properly account for omap data. There is also some storage overhead in the OSD itself
(e.g., bluestore metadata, osdmaps, per-pg metatata). Although, this should work reasonably
well as long as you're not mixing omap and non-omap pools on the same devices but via
different subtrees).

It's probably worth mentioning that the balancer wouldn't actually export/import crush maps.
Instead, once it decides what weight adjustments to make, it will just issue normal monitor
commands to initiate those changes. Currently, there is no option for revert, for practical reasons
because the balancer will make small adjustments to weights over time, but mixed in with that may
be various other changes due to cluster expansion/contraction or admin changes or whatever.

Additionally, the module doesn't over-ride any other configuration, except for `max_misplaced`
parameter in the module, which behaves as the same as `max_change` in `Mon` global configurations.
The `max_misplaced` simply restricts a significant movement of data, all at once.

Enabling
--------

The *balancer* module is enabled with::

  ceph mgr module enable balancer

Configuration
-------------
The basics::

  ceph balancer mode crush-compat|upmap|none

:Description: For versions previous than Luminous 'osd_weight' mode is under-progress.
	      If Ceph version is greater than Luminous v12.2.z, 'upmap' mode is suggested.
	      If Ceph version is Luminous but you wish to be able to generate a backward
	      compatible crush-map, then 'crush-compat' mode is suggested.
:Type: Choices (String)
:Required: Yes.

Usage
-----
A normal user will be expected to just set the mode and turn it on::

  ceph balancer mode crush-compat
  ceph balancer on

An advanced user can play with different optimizer modes etc and see what
they will actually do before making any changes to their cluster.

Automagic mode
--------------
After the user has set the required configuration, he/she can run the balancer
module auto-magically, in which case, the module, will constantly evaluate the unevenness
in the distribution and try to remove it with small incremental changes::

  ceph balancer on

To stop running the balancer module in auto-magic mode::

  ceph balancer off

Checking status
---------------
To see any existing plans, key-weights of different parameters, mode of operation,
key parameter, whether the module is enabled and current max-iterations::

  ceph balancer status

Sample output of `ceph balancer status`::

  {
  "plans": ["planB", "planA"],
  "key-weights": [5, 3, 2],
  "mode": "none",
  "key": "pgs",
  "active": false,
  "max-iterations": 100
  }

Checkng distribution
--------------------
Show analysis of current data distribution on different parameters and average score::

  ceph balancer eval

Sample Output from `ceph balancer eval`::

  current cluster
  target_by_root {'default':
  {0: 0.3333333432674408, 1: 0.3333333432674408, 2: 0.3333333432674408}}
  actual_by_pool {
  cephfs_data_a': {
  'objects': {0: 0.0, 1: 0.0, 2: 0.0},
  'bytes': {0: 0.0, 1: 0.0, 2: 0.0},
  'pgs': {0: 0.3333333333333333, 1: 0.3333333333333333, 2: 0.3333333333333333}}
  (.....)
  stats_by_root {
  'default': {
  'objects': {'avg': 21.0, 'score': 0.0, 'stddev': 7.665050497418206e-07},
  'bytes': {'avg': 2246.0, 'score': 0.0, 'stddev': 8.19795396240023e-05},
  'pgs': {'avg': 16.0, 'score': 0.0, 'stddev': 5.840038465935456e-07}}}
  score_by_pool {}
  score_by_root {'default': {'objects': 0.0, 'bytes': 0.0, 'pgs': 0.0}}
  score 0.000000 (lower is better)


Configuring keys
----------------
To set the parameter on which the optimization is decided::

  ceph balancer key pgs|objects|bytes|auto

:Description: Determines what parameter to optimize upon. If key is auto, then the user
	      may additionally specify key-weights to give relative weights to optimization
	      on these parameters. For example, if the key was pgs, then the balancer module
	      to try to keep the distribution of PGs perfect with respect to the weights of
	      the OSDs, even if that may lead to uneven distribution of bytes.
:Type: Choices (String)
:Required: Yes.
:Default:  pgs

To set key-weights for auto-mode::

  ceph balancer key-weights <pg-weight> <object-weight> <byte-weight>

Where:

``{pg-weight}``, ``{object-weight}``, ``{byte-weight}``

:Description: Specify the weights to each of the three paramters, if the key is auto.
	      In general, it is better to keep the pg-weight as highest, compared to
	      the other two. pg-weight will ensure a better distribution of PG first,
	      hence the chances of uneven distribution creeping up in future gets reduced.
	      The relative priority is left to the user, with an initial relative weights
	      of [5, 3, 2] and in that order. The key-weights have no significance when
	      the key is not `auto`.
:Type: Integer
:Required: Yes
:Default:  [5, 3, 2]

To set max-iterations::

  ceph balancer max-iterations {max-iterations}

Where:

``{max-iterations}``

:Description: The optimization algorithm, uses an iterative algorithm to decide the new
	      weights for the devices. The max-iteration will limit the number of times
	      the iteration will run. More often than not the new weights (converged) are
	      determined before 100 iterations. However, in the odd-case, that results might
	      not converge before 100 iterations, the user may wish to increase to some greater
	      value. Obviously, the higher the value, the more time it requires, in one-off odd-cases.
:Type: Integer
:Required: Yes.
:Default:  100

Plan
====

Each instance of optimization, creates a Plan. A plan takes the configuration of the balancer
module at the time it was created, and creates a new instance of `OSDMap::Incremental`. A plan is
used to store the new-weights that we arrive at, and exposes functionality to evaluate the
expected distribution, should the new weights get executed. When in automagic mode, plans get created,
executed and removed on the fly. However, one can create any number of Plans, as long as each plan
has a different name.

Creating Plan
-------------
To create a new plan to optimize, based on the current mode::

  ceph balancer optimize {plan}

Where:

``{plan}``

:Description: Name of the new plan. The plans currently get lost if ceph-mgr daemon restarts
:Type: String
:Required: Yes.

Evaluating a Plan
-----------------
To roughly predict the resulting distribution, if the plan is executed::

  ceph balancer eval <plan>

To list all the plans that are currently present in the memory, see `Checking Status`_.

To show what the plan would do (basically a dump of cli commands to adjust weights etc)::

  ceph balancer show <plan>

Where:

``{plan}``

:Description: Name of an existing plan.
:Type: String
:Required: Yes.

Executing a Plan
----------------
To execute a plan and then discard it (this will lead to actual movement of data across the OSDs)::

  ceph balancer execute <plan>

Removing a Plan
---------------
To remove a plan from the list of plans::

  ceph balancer rm <plan>
