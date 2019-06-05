.. _governance:

============
 Governance
============

The Ceph open source community is guided by a few different groups.

Project Leader
--------------

The Ceph project is currently led by Sage Weil <sage@redhat.com>.  The
project leader is responsible for guiding the overall direction of the
project and ensuring that the developer and user communities are
healthy.


Committers
----------

Committers are project contributors who have (limited) write access
to the central Ceph code repositories, currently hosted on GitHub.
This group of developers is collectively empowered to make changes to
the Ceph source code.

No individual can make a change in isolation: all code contributions
go through a collaborative review process (and undergo testing) before
being merged.  Currently code review is handled via pull requests on
GitHub.  All pull requests for code going into the Ceph project must
be reviewed and approved by one or more committers before they can be merged.

All pull requests also require some level of testing and high-level
review by the relevant component lead (see below) before they are
merged.  The specifics of this process are dynamic, may vary depending
on which part of the code base is being modified (e.g., documentation
vs test vs core code), and the specifics are evolving over time.

New committers are added to the project (or committers removed from
the project) at the discretion of the Ceph Leadership Team (see below).

The criteria for becoming a committer include:

* a history of non-trivial contributions to the project
* a consistent level of quality
* constructive engagement with other developers when reviewing others'
  code and when having code reviewed

Committers are empowered to:

* making voting code reviews for incoming pull requests.  (Any GitHub
  user may review code, but their reviews are not sufficient to
  satisfy the automated review requirements for merge.)


Ceph Leadership Team
--------------------

The Ceph Leadership Team (CLT) is a collection of component leads and
other core developers who collectively make technical decisions for
the project.  These decisions are generally made by consensus,
although voting may be used if necessary.

The CLT meets weekly via video chat to discuss any pending issues or
decisions.  Minutes for the CLT meetings are published at
`https://pad.ceph.com/p/clt-weekly-minutes <https://pad.ceph.com/p/clt-weekly-minutes>`_.

Committers are added to or removed from the CLT at the discretion of
the CLT itself.

Current CLT members are:

 * Abhishek Lekshmanan <abhishek@suse.com>
 * Alfredo Deza <adeza@redhat.com>
 * Casey Bodley <cbodley@redhat.com>
 * Gregory Farnum <gfarnum@redhat.com>
 * Haomai Wang <haomai@xsky.com>
 * Jason Dillaman <dillaman@redhat.com>
 * Josh Durgin <jdurgin@redhat.com>
 * Joao Eduardo Luis <joao@suse.de>
 * Ken Dreyer <kdreyer@redhat.com>
 * Lenz Grimmer <lgrimmer@suse.com>
 * Matt Benjamin <mbenjami@redhat.com>
 * Myoungwon Oh <omwmw@sk.com>
 * Neha Ojha <nojha@redhat.com>
 * Sage Weil <sage@redhat.com>
 * Sebastian Wagner <swagner@suse.com>
 * Xie Xingguo <xie.xingguo@zte.com.cn>
 * Yehuda Sadeh <yehuda@redhat.com>
 * Zack Cerza <zcerza@redhat.com>

Component Leads
---------------

Each major subcomponent of the Ceph project has a lead engineer who is
responsible for guiding and coordinating development.  The leads are
nominated or appointed at the discretion of the project leader or the
CLT.  Leads responsibilities include:

 * guiding the (usually) daily "stand-up" coordination calls over video chat
 * building the development roadmap for each release cycle
 * coordinating development activity between contributors
 * ensuring that contributions are reviewed
 * ensuring that different proposed changes do not conflict
 * ensuring that testing remains robust (new features include tests, changes do not break tests, etc.)

All component leads are included on the CLT.  They are expected to
report progress and status updates to the rest of the leadership team
and to help facilitate any cross-component coordination of
development.

The Ceph Foundation
-------------------

The Ceph Foundation is organized as a directed fund under the Linux
Foundation and is tasked with supporting the Ceph project community
and ecosystem.  It has no direct control over the technical direction
of the Ceph open source project beyond offering feedback and input
into the collaborative development process.

For more information, see `https://ceph.com/foundation
<https://ceph.com/foundation>`_.

