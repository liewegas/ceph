:note: This is copied verbatim from the old inktank wiki and probably out of date.

Build/Release Notes
===================

Prototype Jenkins Build Service

http://jenkins.front.sepia.ceph.com:8080/

Release process
http://ceph.com/docs/master/dev/release-process/

Draft updates
http://ceph.com/docs/wip-build-doc/dev/release-process/


Release Checklist

What to verify before pushing release

ceph-release has correct URL for repository
Test install of Precise packages
Test install of Centos packages
Verify that ceph --version tag and sha1 match github
Infrastructure notes
There is a squid cache on gitbuilder.ceph.com, it is now configure to always check for new files, but if apt-get is not grabbing the expeceted packages, it could be a problem with date stamps on the packages vs. Squids cache.
Teuthology uses chief recipes in ceph-qa-chef to install packages on systems.  Some of these had hardcoded verion numbers in the past.  The recepies should now be using a repository preference to make sure the right version is installed.
When installing apache2 and related software, apache using a Module Magic Number: 
httpd-mmn = 20051115

httpd-mmn = 20051115-x86-64

to make sure that apache any plugable modules are API compatible.  In some cases the installation history of a machine can cause problems with thes.  Removing apache, then installing apache and the modules together seems to work.



Release Build using Jenkins.

The ceph package is a multi-part job: ceph-setup, ceph-build, ceph-package.  It uses a git mirror hosted on jenkins.  The brnach/tag to be built must be maually updated in ceph-setup before launching the job.  There is a post processing step using scripts in jenkins:~ubuntu to build the repos and push them out.  

The build on jenkins follows the release procedure documented at http://ceph.com/docs/wip-build-doc/dev/release-process/ with the change that version number commits are made in a local mirror on jenkins, then after the build is successful, the commit is pushed to the github repo.

1)  Ensure that the mirror on jenkins is up to date by comparing the commit on the tip of the branch to be built with the corressponding branch on github.

2)  Follow the release process steps for updating the versions numbers and push the tag.

Verify that origin is the jenkins repo

glowell@jenkins:~/build/ceph$ git remote -v show

origin git@jenkins:ceph/ceph (fetch)

origin git@jenkins:ceph/ceph (push)

glowell@jenkins:~/build/ceph$ git push origin dumpling

Counting objects: 9, done.

Compressing objects: 100% (5/5), done.

Writing objects: 100% (5/5), 526 bytes, done.

Total 5 (delta 4), reused 0 (delta 0)

To git@jenkins:ceph/ceph

   5cd66d3..ad85b8b  dumpling -> dumpling

glowell@jenkins:~/build/ceph$ git push origin v0.67.4

Counting objects: 1, done.

Writing objects: 100% (1/1), 808 bytes, done.

Total 1 (delta 0), reused 0 (delta 0)

To git@jenkins:ceph/ceph

 * [new tag]         v0.67.4 -> v0.67.4

====





Creating pbuilder repos on arm



debug1: SSH2_MSG_SERVICE_ACCEPT received

Error reading response length from authentication socket.

SSH_AUTH_SOCK=/tmp/ssh-IQUH9b3cMy/agent.18609

unset SSH_AUTH_SOCK


Repositories
============


Repo Descriptions

Dumpling
    ceph 0.67.2
    ceph-deploy 1.2.3
    python-pushy 0.5.3

Cuttlefish
    ceph 61.8

Bobtail
    ceph 0.56.1

Testing
    ceph 67-rc3
    ceph-deploy  1.2.3
    python-pushy 0.5.3

Ceph-Extras
    libcurl
    qemu


Backported and Upstream Packages
================================


leveldb
snappy
curl
apache
fastcgi
libs3

Background information

Curl

See http://lists.ceph.com/pipermail/ceph-commit-ceph.com/2013-July/002672.html

Newer libcurl supports supports curl_multi_wait(), instead of using select() and forcing a timeout.

Apache and FastCGI

The newer Apache and FastCGI packages are support 100 Continue, which rgw is much better with.

Package dependency matrix
=========================


Squeeze (6.0)
    boost           libboost-1.42.0-4
    leveldb         No upstream package  
                    leveldb-1.7.0  [debian-cuttlefish]
    gperftools      libgoogle-perftools0-1.5-1
    snappy

Wheezy (7.0)
    boost           libboost-1.49.0-3.2
    leveldb         libleveldb1  1.5 (0+20120530.gitdd0d562-1)
                    leveldb-1.12    [ceph-extras]
    gperftools      libgoogle-perftools4-2.0.2
    snappy

Precise
    boost           libboost 1.46.1  (1.46.1-7ubuntu3 )
    leveldb         libleveldb-dev 1.2 (0+20120125.git3c8be10-1) static library only
                    leveldb-1.12    [ceph-extras]
    gperftools      libgoogle-perftools0-1.7-1ubuntu1
    snappy

Quantal
    boost           libboost 1.49 (1.49.0-3.1ubuntu1.2)
    leveldb         libleveldb1 1.5 (0+20120530.gitdd0d562-2)
                    leveldb-1.12    [ceph-extras]
    gperftools      libgoogle-perftools4-2.0-3ubuntu1
    snappy

Raring
    boost           libboost 1.49 (1.49.0-3.2ubuntu1)
    leveldb         libleveldb1 1.12
                    leveldb-1.12    [ceph-extras]
    gperftools      libgoogle-perftools4-2.0-4ubuntu1

Centos6.3
    boost           boost-1.41.0-17.el6_4.x86_64
    leveldb         No upstream package       
                    leveldb-1.7      [epel]
                    leveldb-1.12    [ceph-extras]
    gperftools      gperftools-

Centos6.4
    leveldb         ??
    gperftools      gperftools-

RHEL6.3
    boost          boost-1.41.0-17.el6_4.x86_64  [rhel-x86_64-server-6]
    leveldb        no upstream package         
                   leveldb 1.7     [epel]
                   leveldb-1.12    [ceph-extras]
   gperftools      gperftools-

RHEL6.4
    leveldb        ??
    gperftools     gperftools-

Fedora18
    boost          boost-1.50.0-4.fc18.x86_64
    leveldb        leveldb-devel 1.7
                   leveldb-1.12    [ceph-extras]
    gperftools     gperftools-

Fedora19
    leveldb        leveldb-devel 1.12
    gperftools     gperftools-

Opensuse12.2
    boost          libboost-1.49.0-6.1.2.x86_64
    leveldb        No upstream package
                   leveldb 1.7     [rpm-cuttlefish]
                   leveldb-1.12    [ceph-extras]
    gperftools     gperftools-

Sles11sp2
    boost          boost-devel-1.49.0-81.4
    leveldb        No upstream package      
                   leveldb 1.7 [rpm-cuttlefish]
                   leveldb-1.12    [ceph-extras]
    gperftools     gperftools-


Jenkins build server
====================


The Jenkins build server is located at: http://jenkins.ceph.com/

You can autheticate to Jenkins via the oauth plugin with Google Apps.  Once authenticated, all users have access to all functions.

Build Slaves


The build slaves are listed on the managing nodes page.  Not the Jenkins instance runs as the jenkins user, but the slaves run as jenkins-build user.

Need:
/srv/ceph-build
jenkins-buid user and group
ssh keys for jenkins-build
build subdirectory with
  ceph autobuild and release keys

Packages


rpm build slaves need
rpmbuild
createrepo
yum-utils
build job specific packages

debian build slaves need
reprepro
createrepo
pbuilder
debbuild
build job specific packages

Debugging Build Issues


The overall status of the build is displayed in Jenkins.  The gui allows one to drill down to a specific build on a slave and examine the log file.   The log file is captured and stored on the Jenkins server, not on the build slave.

The work area on the build slave  is located under ~jenkins-build/build/workspace/$JOBNAME.  Additional subdirectories are specific to the job and may be arch name and distro name.   The build area is preserved between builds unless the job does clean up, or appropriate flags are set in the job definition.


Pbuilder hosts

This table is current as of Feb 19 2014:

 	 32-bit	 64-bit
squeeze	mira078	mira062 
wheezy	mira077	mira051
precise	mira084	mira105
quantal	mira083	mira104
raring	mira082	mira096



Building ceph-deploy under jenkins
==================================

I use my home directory either on pudgy or on Jenkins, usually in a subdirectory ~glowell/build/ceph-deploy-$version}

# setup environment
export GNUPGHOME=~glowell/build/gnupg.ceph-release
export DEBEMAIL=gary.lowell@inktank.com
export DEBFULLNAME="Gary Lowell"

# Verify that release key is available
gpg --list-keys
/home/glowell/build/gnupg.ceph-release/pubring.gpg
--------------------------------------------------
pub   4096R/17ED316D 2012-05-20
uid                  Ceph Release Key <sage@newdream.net>

#  Clone a fresh copy of  ceph-deploy
git clone git@github.com:ceph/ceph-deploy
cd ceph-deploy/

# verify that setup.py has correct version number (This should be set by developer when new release is ready)         
grep version ceph_deploy/__init__.py
__version__ = '1.x.x'

# set the rpm version number
vi ceph-deploy.spec  

# Update the debian change log.  Text is "new upstream version"  (Note the -1 suffix that is the debian build number)
dch -v  1.x.x-1

#  Commit changes  message is "v1.x.x"
git commit -a

# Tag the release with annotated, signed tag
/srv/ceph-build/tag_release.sh v1.x.x

# Push the tag
git push origin master
git push origin v1.x.x

#From the jenkins dashboard at http://jenkins.ceph.com, select the ceph-deploy tab, and the ceph-deploy job
#Select build now, and enter the version number

#After build completes
#Log in to ubuntu@jenkins
#There are two scripts, add_cdep_to_{rpm,deb}_repos.sh, that will add the ceph-deploy packages to the local mirrors of the ceph.com repos.

./add_cdep_to_deb_repos.sh
./add_cdep_to_rpm_repos.sh

#  After adding the packages to the local repos, the rpm packages will need to be signed, and the repo index rebuilt.
#
/srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-emperor
/srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-dumpling
/srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-cuttlefish
/srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-testing
/srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-firefly
/srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-giant

# After adding the packages and signing the rpms the repos are rsynced to ceph.com
# The script syncs all the locally mirror repos.
./sync-push.sh
 

Ceph build log
==============

Log 1:  Update version numbers, tag release, and push tag to jenkins mirror


Script started on Mon 30 Dec 2013 08:51:54 PM UTC

glowell@jenkins: ~/build/ceph-0.74$ git clone git@jenkins:ceph/ceph
Cloning into 'ceph'...
remote: Counting objects: 221886, done.K
remote: Compressing objects: 100% (44816/44816),Kdone.K
remote:nTotale2218869(delta9179912),8reused.216267|(delta1175135)K
Receiving objects: 100% (221886/221886), 44.85 MiB | 10.11 MiB/s, done.
Resolving deltas: 100% (179912/179912), done.

glowell@jenkins: ~/build/ceph-0.74$ cd ceph/

glowell@jenkins: ~/build/ceph-0.74/ceph$ git remote rename origin jenkins

glowell@jenkins: ~/build/ceph-0.74/ceph$ git remote -v
jenkins git@jenkins:ceph/ceph (fetch)
jenkins git@jenkins:ceph/ceph (push)

glowell@jenkins: ~/build/ceph-0.74/ceph$ export GNUPGHOME=~/build/gnupg.ceph-release/
glowell@jenkins: ~/build/ceph-0.74/ceph$ gpg --list-keys
/home/glowell/build/gnupg.ceph-release//pubring.gpg
---------------------------------------------------
pub   4096R/17ED316D 2012-05-20
uid      Ceph Release Key <sage@newdream.net>

pub   1024D/288995C8 2010-01-21
uid      Sage Weil <sage@newdream.net>
uid      Sage Weil <sage@kernel.org>
sub   4096R/4E45A6A1 2010-01-21
sub   4096g/CCFE8404 2010-01-21

glowell@jenkins: ~/build/ceph-0.74/ceph$ git checkout next
Branch next set up to track remote branch next from jenkins.
Switched to a new branch 'next'

glowell@jenkins: ~/build/ceph-0.74/ceph$ git submodule update --init
Submodule 'ceph-object-corpus' (git://ceph.com/git/ceph-object-corpus.git) registered for path 'ceph-object-corpus'
Submodule 'src/libs3' (git://github.com/ceph/libs3.git) registered for path 'src/libs3'
Cloning into 'ceph-object-corpus'...
remote: Counting objects: 10844, done.K
remote: Compressing objects: 100% (6400/6400),Kdone.K
remote:nTotale10844 (delta01423),4reused 10461 (delta 1085)K
Receiving objects: 100% (10844/10844), 1.57 MiB, done.
Resolving deltas: 100% (1423/1423), done.
Submodule path 'ceph-object-corpus': checked out '84a153afa71c4468c7a3d78270af6415d0a1c3e7'
Cloning into 'src/libs3'...
remote: Reusing existing pack: 1000, done.K
remote:nTotale1000 (delta90),1reused308(deltaB0)K559 KiB/s
Receiving objects: 100% (1000/1000),3356.47KKiB||5559KKiB/s, done.
Resolving deltas: 100% (719/719), done.
Submodule path 'src/libs3': checked out '9dc3a9c683385abfe4ad92b7c6ff30719acc3c13'

glowell@jenkins: ~/build/ceph-0.74/ceph$ vi configure.ac

glowell@jenkins: ~/build/ceph-0.74/ceph$ dch -v 0.74-1

glowell@jenkins: ~/build/ceph-0.74/ceph$ git diff
diff --git a/configure.ac b/configure.ac
index ab2e49d..d95fad6 100644
--- a/configure.ac
+++ b/configure.ac
@@ -8,7 +8,7 @@ AC_PREREQ(2.59)
 # VERSION define is not used by the code.  It gets a version string
 # from 'git describe'; see src/ceph_ver.[ch]
 
-AC_INIT([ceph], [0.73], [ceph-devel@vger.kernel.org])
+AC_INIT([ceph], [0.74], [ceph-devel@vger.kernel.org])
 
 # Create release string.  Used with VERSION for RPMs.
 RPM_RELEASE=0
diff --git a/debian/changelog b/debian/changelog
index 82d7667..a934026 100644
--- a/debian/changelog
+++ b/debian/changelog
@@ -1,3 +1,9 @@
+ceph (0.74-1) stable; urgency=low
+
+  * New upstream release 
+
+ -- Gary Lowell <glowell@jenkins.front.sepia.ceph.com>  Mon, 30 Dec 2013 21:02:35 +0000
+
 ceph (0.73-1) precise; urgency=low
 
   * New upstream release 

glowell@jenkins: ~/build/ceph-0.74/ceph$ git commit -a
### commit message "v0.74"
 2 files changed, 7 insertions(+), 1 deletion(-)

glowell@jenkins: ~/build/ceph-0.74/ceph$ /srv/ceph-build/tag_release.sh v0.74

glowell@jenkins: ~/build/ceph-0.74/ceph$ git push jenkins next
Counting objects: 9, done.
Compressing objects: 100% (5/5), done.
Writing objects: 100% (5/5), 516 bytes, done.
Total 5 (delta 4), reused 0 (delta 0)
Auto packing the repository for optimum performance.
To git@jenkins:ceph/ceph
   4f07848..c165483  next -> next

glowell@jenkins: ~/build/ceph-0.74/ceph$ git push jenkins v0.74
Counting objects: 1, done.
Writing objects: 100% (1/1), 802 bytes, done.
Total 1 (delta 0), reused 0 (delta 0)
To git@jenkins:ceph/ceph
 * [new tag]      v0.74 -> v0.74

glowell@jenkins: ~/build/ceph-0.74/ceph$ exit

Script done on Wed 01 Jan 2014 07:51:16 AM UTC
Log 2:  Create repos, sign, and sync to ceph.com

There is a mirror of the ceph.com repos in jenkins:~ubuntu/repos.  The built packages are added to the local repo, then rsynced to ceph.com.

Script started on Wed 01 Jan 2014 05:31:30 AM UTC

ubuntu@jenkins: ~ubuntu@jenkins:~$ ./add_ceph_tarballs.sh
/home2/jenkins/jobs/ceph-setup/lastSuccessful/archive/dist/ceph-0.74.tar.bz2
/home2/jenkins/jobs/ceph-setup/lastSuccessful/archive/dist/ceph-0.74.tar.gz

ubuntu@jenkins: ~ubuntu@jenkins:~$ ./add_ceph_tarballs.sh copy
ceph-0.74.tar.bz2  100% 3403KB   3.3MB/s  00:00 ETA
ceph-0.74.tar.gz  100% 4477KB   4.4MB/s  00:00 ETA

ubuntu@jenkins: ~ubuntu@jenkins:~$ export GNUPGHOME=/home/ubuntu/glowell/gnupg.ceph-release/
ubuntu@jenkins: ~ubuntu@jenkins:~$ gpg --list-key
/home/ubuntu/glowell/gnupg.ceph-release//pubring.gpg
----------------------------------------------------
pub   4096R/17ED316D 2012-05-20
uid      Ceph Release Key <sage@newdream.net>

pub   1024D/288995C8 2010-01-21
uid      Sage Weil <sage@newdream.net>
uid      Sage Weil <sage@kernel.org>
sub   4096R/4E45A6A1 2010-01-21
sub   4096g/CCFE8404 2010-01-21

ubuntu@jenkins: ~ubuntu@jenkins:~$ grep ^CODENAME add_ceph.sh
CODENAME=testing

ubuntu@jenkins: ~ubuntu@jenkins:~$ ./add_ceph.sh
Signing packages and repo with 17ED316D
** Adding debian packages for dist=quantal, arch=armv7l
    Adding sourceceph_0.74-1.dsc
/home/jenkins-build/build/workspace/ceph-package/Arch=armv7l,Distro=quantal-pbuild/dist ~
Exporting indices...
Deleting files no longer referenced...
~
    Changes: ceph_0.74-1quantal_armhf.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=armv7l,Distro=quantal-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=raring, arch=armv7l
    Changes: ceph_0.74-1raring_armhf.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=armv7l,Distro=raring-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=precise, arch=i386
    Changes: ceph_0.74-1precise_i386.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=i386,Distro=precise-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=quantal, arch=i386
    Changes: ceph_0.74-1quantal_i386.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=i386,Distro=quantal-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=raring, arch=i386
    Changes: ceph_0.74-1raring_i386.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=i386,Distro=raring-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=squeeze, arch=i386
    Changes: ceph_0.74-1~bpo60+1_i386.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=i386,Distro=squeeze-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=wheezy, arch=i386
    Changes: ceph_0.74-1~bpo70+1_i386.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=i386,Distro=wheezy-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding rpm packages for dist=centos6.3, arch=x86_64
    Copying centos6.3 to el6

** Adding rpm packages for dist=fedora18, arch=x86_64
    Copying fedora18 to fc18

** Adding rpm packages for dist=fedora19, arch=x86_64
    Copying fedora19 to fc19

** Adding rpm packages for dist=opensuse12.2, arch=x86_64
    Copying opensuse12.2 to opensuse12.2

** Adding debian packages for dist=precise, arch=x86_64
    Changes: ceph_0.74-1precise_amd64.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=x86_64,Distro=precise-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=quantal, arch=x86_64
    Changes: ceph_0.74-1quantal_amd64.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=x86_64,Distro=quantal-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=raring, arch=x86_64
    Changes: ceph_0.74-1raring_amd64.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=x86_64,Distro=raring-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding rpm packages for dist=rhel6.3, arch=x86_64
    Copying rhel6.3 to rhel6

** Adding rpm packages for dist=sles11sp2, arch=x86_64
    Copying sles11sp2 to sles11

** Adding debian packages for dist=squeeze, arch=x86_64
    Changes: ceph_0.74-1~bpo60+1_amd64.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=x86_64,Distro=squeeze-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~

** Adding debian packages for dist=wheezy, arch=x86_64
    Changes: ceph_0.74-1~bpo70+1_amd64.changes
/home/jenkins-build/build/workspace/ceph-package/Arch=x86_64,Distro=wheezy-pbuild/dist/debian ~
Exporting indices...
Deleting files no longer referenced...
~


### lines begining "skipping" for already signed rpms in the repo have been flitered out
ubuntu@jenkins: ~ubuntu@jenkins:~$ /srv/ceph-build/sign_and_index_rpm_repo.sh repos repos rpm-testing
version rpm-testing
signing rpms, version rpm-testing key 17ED316D
signing:  repos/rpm-testing/sles11/SRPMS/ceph-0.74-0.src.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/SRPMS/ceph-0.74-0.src.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/SRPMS/ceph-0.74-0.src.rpm:
signing:  repos/rpm-testing/sles11/x86_64/librados2-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/librados2-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/librados2-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-radosgw-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-radosgw-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-radosgw-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-test-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-test-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-test-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-debugsource-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-debugsource-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-debugsource-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/cephfs-java-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/cephfs-java-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/cephfs-java-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/librbd1-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/librbd1-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/librbd1-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-fuse-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-fuse-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-fuse-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-devel-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-devel-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-devel-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/rest-bench-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/rest-bench-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/rest-bench-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/rbd-fuse-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/rbd-fuse-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/rbd-fuse-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/python-ceph-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/python-ceph-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/python-ceph-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/sles11/x86_64/libcephfs1-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/sles11/x86_64/libcephfs1-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/sles11/x86_64/libcephfs1-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/el6/SRPMS/ceph-0.74-0.el6.src.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/SRPMS/ceph-0.74-0.el6.src.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/SRPMS/ceph-0.74-0.el6.src.rpm:
signing:  repos/rpm-testing/el6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/ceph-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/ceph-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/ceph-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/librados2-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/librados2-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/librados2-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/librbd1-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/librbd1-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/librbd1-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/el6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/el6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/el6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/SRPMS/ceph-0.74-0.el6.src.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/SRPMS/ceph-0.74-0.el6.src.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/SRPMS/ceph-0.74-0.el6.src.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/ceph-debuginfo-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/ceph-debuginfo-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/ceph-debuginfo-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/ceph-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/ceph-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/ceph-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/librados2-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/librados2-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/librados2-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/librbd1-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/librbd1-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/librbd1-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/rhel6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/rhel6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/rhel6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm:
signing:  repos/rpm-testing/fc18/SRPMS/ceph-0.74-0.fc18.src.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/SRPMS/ceph-0.74-0.fc18.src.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/SRPMS/ceph-0.74-0.fc18.src.rpm:
signing:  repos/rpm-testing/fc18/x86_64/libcephfs_jni1-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/libcephfs_jni1-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/libcephfs_jni1-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/ceph-test-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/ceph-test-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/ceph-test-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/ceph-devel-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/ceph-devel-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/ceph-devel-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/librados2-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/librados2-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/librados2-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/cephfs-java-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/cephfs-java-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/cephfs-java-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/ceph-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/ceph-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/ceph-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/ceph-fuse-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/ceph-fuse-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/ceph-fuse-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/rest-bench-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/rest-bench-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/rest-bench-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/rbd-fuse-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/rbd-fuse-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/rbd-fuse-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/librbd1-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/librbd1-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/librbd1-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/python-ceph-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/python-ceph-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/python-ceph-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/libcephfs1-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/libcephfs1-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/libcephfs1-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc18/x86_64/ceph-radosgw-0.74-0.fc18.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc18/x86_64/ceph-radosgw-0.74-0.fc18.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc18/x86_64/ceph-radosgw-0.74-0.fc18.x86_64.rpm:
signing:  repos/rpm-testing/fc19/SRPMS/ceph-0.74-0.fc19.src.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/SRPMS/ceph-0.74-0.fc19.src.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/SRPMS/ceph-0.74-0.fc19.src.rpm:
signing:  repos/rpm-testing/fc19/x86_64/ceph-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/ceph-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/ceph-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/rest-bench-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/rest-bench-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/rest-bench-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/python-ceph-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/python-ceph-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/python-ceph-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/rbd-fuse-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/rbd-fuse-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/rbd-fuse-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/ceph-radosgw-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/ceph-radosgw-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/ceph-radosgw-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/libcephfs1-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/libcephfs1-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/libcephfs1-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/ceph-devel-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/ceph-devel-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/ceph-devel-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/libcephfs_jni1-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/libcephfs_jni1-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/libcephfs_jni1-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/cephfs-java-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/cephfs-java-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/cephfs-java-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/librados2-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/librados2-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/librados2-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/ceph-fuse-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/ceph-fuse-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/ceph-fuse-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/ceph-test-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/ceph-test-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/ceph-test-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/fc19/x86_64/librbd1-0.74-0.fc19.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/fc19/x86_64/librbd1-0.74-0.fc19.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/fc19/x86_64/librbd1-0.74-0.fc19.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/SRPMS/ceph-0.74-0.src.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/SRPMS/ceph-0.74-0.src.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/SRPMS/ceph-0.74-0.src.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/librados2-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/librados2-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/librados2-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-radosgw-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-radosgw-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-radosgw-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-test-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-test-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-test-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/librados2-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/librados2-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/librados2-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-radosgw-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-radosgw-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-radosgw-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-debugsource-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-debugsource-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-debugsource-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-test-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-test-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-test-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/libcephfs_jni1-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/libcephfs_jni1-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/libcephfs_jni1-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/libcephfs1-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/libcephfs1-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/libcephfs1-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/cephfs-java-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/cephfs-java-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/cephfs-java-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/librbd1-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/librbd1-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/librbd1-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-fuse-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-fuse-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-fuse-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-devel-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-devel-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-devel-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-fuse-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-fuse-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-fuse-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/rest-bench-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/rest-bench-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/rest-bench-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/rbd-fuse-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/rbd-fuse-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/rbd-fuse-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/rest-bench-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/rest-bench-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/rest-bench-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/librbd1-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/librbd1-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/librbd1-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/rbd-fuse-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/rbd-fuse-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/rbd-fuse-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/python-ceph-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/python-ceph-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/python-ceph-0.74-0.x86_64.rpm:
signing:  repos/rpm-testing/opensuse12.2/x86_64/libcephfs1-0.74-0.x86_64.rpm
spawn rpm --addsign --define _gpg_name 17ED316D repos/rpm-testing/opensuse12.2/x86_64/libcephfs1-0.74-0.x86_64.rpm
Enter pass phrase:
Pass phrase is good.
repos/rpm-testing/opensuse12.2/x86_64/libcephfs1-0.74-0.x86_64.rpm:
done
indexing repos/rpm-testing/el6/noarch
19/19--rpython-pushy-0.5.1-6.1.noarch.rpm.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/el6/SRPMS
27/27--cceph-0.64-0.el6.src.rpmpmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/el6/x86_64
321/321--cceph-devel-0.72-rc1.el6.x86_64.rpmpmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc17/noarch
1/1 - ceph-release-1-0.noarch.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc17/SRPMS
1/1 - ceph-0.62-0.fc17.src.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc17/x86_64
132/132--rceph-radosgw-0.62-0.fc17.x86_64.rpmpmm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc18/noarch
15/15--cceph-deploy-1.2.1-0.noarch.rpmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc18/SRPMS
27/27--cpushy-0.5.3-1.src.rpmc.rpmm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc18/x86_64
249/249--llibrbd1-0.67-rc2.fc18.x86_64.rpmmrpmmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc19/noarch
14/14--cceph-deploy-1.2.1-0.noarch.rpmm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc19/SRPMS
21/21--cceph-0.67-rc3.fc19.src.rpmm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/fc19/x86_64
105/105--clibrbd1-0.72-rc1.fc19.x86_64.rpmpmmmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/opensuse12.2/noarch
14/14--cceph-deploy-1.2.1-0.noarch.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/opensuse12.2/SRPMS
27/27--cpushy-0.5.3-1.src.rpmrc.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/opensuse12.2/x86_64
459/459--crest-bench-debuginfo-0.67-rc3.x86_64.rpm.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/opensuse12/x86_64
45/45--llibrbd1-0.56-0.x86_64.rpmpmx86_64.rpmpmmpmm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/rhel6/noarch
16/16--cpython-pushy-0.5.1-6.1.noarch.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/rhel6/SRPMS
21/21--cceph-0.69-0.el6.src.rpmpmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/rhel6/x86_64
112/112--lceph-devel-0.72-rc1.el6.x86_64.rpmpmm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/sles11/noarch
3/3 - ceph-release-1-0.noarch.rpmrpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/sles11/SRPMS
27/27--cpushy-0.5.3-1.src.rpmrc.rpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
indexing repos/rpm-testing/sles11/x86_64
347/347--clibrbd1-0.56-0.x86_64.rpmpmrpm.rpmpm
Saving Primary metadata
Saving file lists metadata
Saving other metadata
done

ubuntu@jenkins: ~ubuntu@jenkins:~$ ./sync-push.sh
***  syncing ceph-extras debian ***

sending incremental file list

sent 11099 bytes  received 105 bytes  7469.33 bytes/sec
total size is 38575459 speedup is 3443.01
***  syncing ceph-extras rpm ***

sending incremental file list

sent 28682 bytes  received 69 bytes  57502.00 bytes/sec
total size is 126094531  speedup is 4385.74
***  syncing emperor debian ***

sending incremental file list

sent 77131 bytes  received 67 bytes  51465.33 bytes/sec
total size is 30162170047  speedup is 390711.81
***  syncing emperor rpm ***

sending incremental file list

sent 30920 bytes  received 54 bytes  61948.00 bytes/sec
total size is 4385280903  speedup is 141579.42
***  syncing dumpling debian ***

sending incremental file list

sent 118234 bytes  received 68 bytes  236604.00 bytes/sec
total size is 43706600623  speedup is 369449.38
***  syncing dumpling rpm ***

sending incremental file list

sent 106132 bytes  received 54 bytes  70790.67 bytes/sec
total size is 11402871941  speedup is 107385.83
***  syncing cuttlefish debian ***

sending incremental file list

sent 187293 bytes  received 75 bytes  374736.00 bytes/sec
total size is 55960369127  speedup is 298665.56
***  syncing cuttlefish rpm ***

sending incremental file list

sent 72526 bytes  received 61 bytes  145174.00 bytes/sec
total size is 10980660643  speedup is 151275.86
***  syncing testing debian ***

sending incremental file list
db/
db/checksums.db
db/contents.cache.db
db/packages.db
db/references.db
db/release.caches.db
db/version
dists/precise/
dists/precise/InRelease
dists/precise/Release
dists/precise/Release.gpg
dists/precise/main/
dists/precise/main/Contents-amd64.bz2
dists/precise/main/Contents-amd64.gz
dists/precise/main/Contents-armhf.bz2
dists/precise/main/Contents-armhf.gz
dists/precise/main/Contents-i386.bz2
dists/precise/main/Contents-i386.gz
dists/precise/main/binary-amd64/
dists/precise/main/binary-amd64/Packages
dists/precise/main/binary-amd64/Packages.bz2
dists/precise/main/binary-amd64/Packages.gz
dists/precise/main/binary-armhf/
dists/precise/main/binary-armhf/Packages
dists/precise/main/binary-armhf/Packages.bz2
dists/precise/main/binary-armhf/Packages.gz
dists/precise/main/binary-i386/
dists/precise/main/binary-i386/Packages
dists/precise/main/binary-i386/Packages.bz2
dists/precise/main/binary-i386/Packages.gz
dists/quantal/
dists/quantal/InRelease
dists/quantal/Release
dists/quantal/Release.gpg
dists/quantal/main/
dists/quantal/main/Contents-amd64.bz2
dists/quantal/main/Contents-amd64.gz
dists/quantal/main/Contents-armhf.bz2
dists/quantal/main/Contents-armhf.gz
dists/quantal/main/Contents-i386.bz2
dists/quantal/main/Contents-i386.gz
dists/quantal/main/binary-amd64/
dists/quantal/main/binary-amd64/Packages
dists/quantal/main/binary-amd64/Packages.bz2
dists/quantal/main/binary-amd64/Packages.gz
dists/quantal/main/binary-armhf/
dists/quantal/main/binary-armhf/Packages
dists/quantal/main/binary-armhf/Packages.bz2
dists/quantal/main/binary-armhf/Packages.gz
dists/quantal/main/binary-i386/
dists/quantal/main/binary-i386/Packages
dists/quantal/main/binary-i386/Packages.bz2
dists/quantal/main/binary-i386/Packages.gz
dists/quantal/main/source/
dists/quantal/main/source/Sources.bz2
dists/quantal/main/source/Sources.gz
dists/raring/
dists/raring/InRelease
dists/raring/Release
dists/raring/Release.gpg
dists/raring/main/
dists/raring/main/Contents-amd64.bz2
dists/raring/main/Contents-amd64.gz
dists/raring/main/Contents-armhf.bz2
dists/raring/main/Contents-armhf.gz
dists/raring/main/Contents-i386.bz2
dists/raring/main/Contents-i386.gz
dists/raring/main/binary-amd64/
dists/raring/main/binary-amd64/Packages
dists/raring/main/binary-amd64/Packages.bz2
dists/raring/main/binary-amd64/Packages.gz
dists/raring/main/binary-armhf/
dists/raring/main/binary-armhf/Packages
dists/raring/main/binary-armhf/Packages.bz2
dists/raring/main/binary-armhf/Packages.gz
dists/raring/main/binary-i386/
dists/raring/main/binary-i386/Packages
dists/raring/main/binary-i386/Packages.bz2
dists/raring/main/binary-i386/Packages.gz
dists/squeeze/
dists/squeeze/InRelease
dists/squeeze/Release
dists/squeeze/Release.gpg
dists/squeeze/main/
dists/squeeze/main/Contents-amd64.bz2
dists/squeeze/main/Contents-amd64.gz
dists/squeeze/main/Contents-armhf.bz2
dists/squeeze/main/Contents-armhf.gz
dists/squeeze/main/Contents-i386.bz2
dists/squeeze/main/Contents-i386.gz
dists/squeeze/main/binary-amd64/
dists/squeeze/main/binary-amd64/Packages
dists/squeeze/main/binary-amd64/Packages.bz2
dists/squeeze/main/binary-amd64/Packages.gz
dists/squeeze/main/binary-armhf/
dists/squeeze/main/binary-armhf/Packages
dists/squeeze/main/binary-armhf/Packages.bz2
dists/squeeze/main/binary-armhf/Packages.gz
dists/squeeze/main/binary-i386/
dists/squeeze/main/binary-i386/Packages
dists/squeeze/main/binary-i386/Packages.bz2
dists/squeeze/main/binary-i386/Packages.gz
dists/wheezy/
dists/wheezy/InRelease
dists/wheezy/Release
dists/wheezy/Release.gpg
dists/wheezy/main/
dists/wheezy/main/Contents-amd64.bz2
dists/wheezy/main/Contents-amd64.gz
dists/wheezy/main/Contents-armhf.bz2
dists/wheezy/main/Contents-armhf.gz
dists/wheezy/main/Contents-i386.bz2
dists/wheezy/main/Contents-i386.gz
dists/wheezy/main/binary-amd64/
dists/wheezy/main/binary-amd64/Packages
dists/wheezy/main/binary-amd64/Packages.bz2
dists/wheezy/main/binary-amd64/Packages.gz
dists/wheezy/main/binary-armhf/
dists/wheezy/main/binary-armhf/Packages
dists/wheezy/main/binary-armhf/Packages.bz2
dists/wheezy/main/binary-armhf/Packages.gz
dists/wheezy/main/binary-i386/
dists/wheezy/main/binary-i386/Packages
dists/wheezy/main/binary-i386/Packages.bz2
dists/wheezy/main/binary-i386/Packages.gz
pool/main/c/ceph/
pool/main/c/ceph/ceph-common-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-common-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-common_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-common_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-common_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-common_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-common_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-common_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-common_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-common_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-common_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-common_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-common_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-common_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-fs-common-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-fs-common_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-fs-common_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-fs-common_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-fs-common_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-fs-common_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-fs-common_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-fs-common_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-fs-common_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-fs-common_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-fs-common_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-fs-common_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-fs-common_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-fuse-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-fuse_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-fuse_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-fuse_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-fuse_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-fuse_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-fuse_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-fuse_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-fuse_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-fuse_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-fuse_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-fuse_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-fuse_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-mds-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-mds_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-mds_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-mds_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-mds_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-mds_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-mds_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-mds_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-mds_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-mds_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-mds_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-mds_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-mds_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-resource-agents_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-test-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph-test_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph-test_0.74-1precise_i386.deb
pool/main/c/ceph/ceph-test_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph-test_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph-test_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph-test_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph-test_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph-test_0.74-1raring_i386.deb
pool/main/c/ceph/ceph-test_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph-test_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph-test_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph-test_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph_0.74-1.diff.gz
pool/main/c/ceph/ceph_0.74-1.dsc
pool/main/c/ceph/ceph_0.74-1precise_amd64.deb
pool/main/c/ceph/ceph_0.74-1precise_i386.deb
pool/main/c/ceph/ceph_0.74-1quantal_amd64.deb
pool/main/c/ceph/ceph_0.74-1quantal_armhf.deb
pool/main/c/ceph/ceph_0.74-1quantal_i386.deb
pool/main/c/ceph/ceph_0.74-1raring_amd64.deb
pool/main/c/ceph/ceph_0.74-1raring_armhf.deb
pool/main/c/ceph/ceph_0.74-1raring_i386.deb
pool/main/c/ceph/ceph_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/ceph_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/ceph_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/ceph_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/ceph_0.74.orig.tar.gz
pool/main/c/ceph/libcephfs-dev_0.74-1precise_amd64.deb
pool/main/c/ceph/libcephfs-dev_0.74-1precise_i386.deb
pool/main/c/ceph/libcephfs-dev_0.74-1quantal_amd64.deb
pool/main/c/ceph/libcephfs-dev_0.74-1quantal_armhf.deb
pool/main/c/ceph/libcephfs-dev_0.74-1quantal_i386.deb
pool/main/c/ceph/libcephfs-dev_0.74-1raring_amd64.deb
pool/main/c/ceph/libcephfs-dev_0.74-1raring_armhf.deb
pool/main/c/ceph/libcephfs-dev_0.74-1raring_i386.deb
pool/main/c/ceph/libcephfs-dev_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/libcephfs-dev_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/libcephfs-dev_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/libcephfs-dev_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/libcephfs-java_0.74-1precise_all.deb
pool/main/c/ceph/libcephfs-java_0.74-1quantal_all.deb
pool/main/c/ceph/libcephfs-java_0.74-1raring_all.deb
pool/main/c/ceph/libcephfs-java_0.74-1~bpo60+1_all.deb
pool/main/c/ceph/libcephfs-java_0.74-1~bpo70+1_all.deb
pool/main/c/ceph/libcephfs-jni_0.74-1precise_amd64.deb
pool/main/c/ceph/libcephfs-jni_0.74-1precise_i386.deb
pool/main/c/ceph/libcephfs-jni_0.74-1quantal_amd64.deb
pool/main/c/ceph/libcephfs-jni_0.74-1quantal_armhf.deb
pool/main/c/ceph/libcephfs-jni_0.74-1quantal_i386.deb
pool/main/c/ceph/libcephfs-jni_0.74-1raring_amd64.deb
pool/main/c/ceph/libcephfs-jni_0.74-1raring_armhf.deb
pool/main/c/ceph/libcephfs-jni_0.74-1raring_i386.deb
pool/main/c/ceph/libcephfs-jni_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/libcephfs-jni_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/libcephfs-jni_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/libcephfs-jni_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/libcephfs1-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/libcephfs1_0.74-1precise_amd64.deb
pool/main/c/ceph/libcephfs1_0.74-1precise_i386.deb
pool/main/c/ceph/libcephfs1_0.74-1quantal_amd64.deb
pool/main/c/ceph/libcephfs1_0.74-1quantal_armhf.deb
pool/main/c/ceph/libcephfs1_0.74-1quantal_i386.deb
pool/main/c/ceph/libcephfs1_0.74-1raring_amd64.deb
pool/main/c/ceph/libcephfs1_0.74-1raring_armhf.deb
pool/main/c/ceph/libcephfs1_0.74-1raring_i386.deb
pool/main/c/ceph/libcephfs1_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/libcephfs1_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/libcephfs1_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/libcephfs1_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/librados-dev_0.74-1precise_amd64.deb
pool/main/c/ceph/librados-dev_0.74-1precise_i386.deb
pool/main/c/ceph/librados-dev_0.74-1quantal_amd64.deb
pool/main/c/ceph/librados-dev_0.74-1quantal_armhf.deb
pool/main/c/ceph/librados-dev_0.74-1quantal_i386.deb
pool/main/c/ceph/librados-dev_0.74-1raring_amd64.deb
pool/main/c/ceph/librados-dev_0.74-1raring_armhf.deb
pool/main/c/ceph/librados-dev_0.74-1raring_i386.deb
pool/main/c/ceph/librados-dev_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/librados-dev_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/librados-dev_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/librados-dev_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/librados2-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/librados2-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/librados2-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/librados2-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/librados2-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/librados2-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/librados2-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/librados2-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/librados2-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/librados2-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/librados2-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/librados2-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/librados2_0.74-1precise_amd64.deb
pool/main/c/ceph/librados2_0.74-1precise_i386.deb
pool/main/c/ceph/librados2_0.74-1quantal_amd64.deb
pool/main/c/ceph/librados2_0.74-1quantal_armhf.deb
pool/main/c/ceph/librados2_0.74-1quantal_i386.deb
pool/main/c/ceph/librados2_0.74-1raring_amd64.deb
pool/main/c/ceph/librados2_0.74-1raring_armhf.deb
pool/main/c/ceph/librados2_0.74-1raring_i386.deb
pool/main/c/ceph/librados2_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/librados2_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/librados2_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/librados2_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/librbd-dev_0.74-1precise_amd64.deb
pool/main/c/ceph/librbd-dev_0.74-1precise_i386.deb
pool/main/c/ceph/librbd-dev_0.74-1quantal_amd64.deb
pool/main/c/ceph/librbd-dev_0.74-1quantal_armhf.deb
pool/main/c/ceph/librbd-dev_0.74-1quantal_i386.deb
pool/main/c/ceph/librbd-dev_0.74-1raring_amd64.deb
pool/main/c/ceph/librbd-dev_0.74-1raring_armhf.deb
pool/main/c/ceph/librbd-dev_0.74-1raring_i386.deb
pool/main/c/ceph/librbd-dev_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/librbd-dev_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/librbd-dev_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/librbd-dev_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/librbd1-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/librbd1-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/librbd1-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/librbd1-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/librbd1-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/librbd1-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/librbd1-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/librbd1-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/librbd1-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/librbd1-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/librbd1-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/librbd1-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/librbd1_0.74-1precise_amd64.deb
pool/main/c/ceph/librbd1_0.74-1precise_i386.deb
pool/main/c/ceph/librbd1_0.74-1quantal_amd64.deb
pool/main/c/ceph/librbd1_0.74-1quantal_armhf.deb
pool/main/c/ceph/librbd1_0.74-1quantal_i386.deb
pool/main/c/ceph/librbd1_0.74-1raring_amd64.deb
pool/main/c/ceph/librbd1_0.74-1raring_armhf.deb
pool/main/c/ceph/librbd1_0.74-1raring_i386.deb
pool/main/c/ceph/librbd1_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/librbd1_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/librbd1_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/librbd1_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/python-ceph_0.74-1precise_amd64.deb
pool/main/c/ceph/python-ceph_0.74-1precise_i386.deb
pool/main/c/ceph/python-ceph_0.74-1quantal_amd64.deb
pool/main/c/ceph/python-ceph_0.74-1quantal_armhf.deb
pool/main/c/ceph/python-ceph_0.74-1quantal_i386.deb
pool/main/c/ceph/python-ceph_0.74-1raring_amd64.deb
pool/main/c/ceph/python-ceph_0.74-1raring_armhf.deb
pool/main/c/ceph/python-ceph_0.74-1raring_i386.deb
pool/main/c/ceph/python-ceph_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/python-ceph_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/python-ceph_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/python-ceph_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/radosgw-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/radosgw-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/radosgw-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/radosgw-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/radosgw-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/radosgw-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/radosgw-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/radosgw-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/radosgw-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/radosgw-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/radosgw-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/radosgw-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/radosgw_0.74-1precise_amd64.deb
pool/main/c/ceph/radosgw_0.74-1precise_i386.deb
pool/main/c/ceph/radosgw_0.74-1quantal_amd64.deb
pool/main/c/ceph/radosgw_0.74-1quantal_armhf.deb
pool/main/c/ceph/radosgw_0.74-1quantal_i386.deb
pool/main/c/ceph/radosgw_0.74-1raring_amd64.deb
pool/main/c/ceph/radosgw_0.74-1raring_armhf.deb
pool/main/c/ceph/radosgw_0.74-1raring_i386.deb
pool/main/c/ceph/radosgw_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/radosgw_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/radosgw_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/radosgw_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/rbd-fuse-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/rbd-fuse_0.74-1precise_amd64.deb
pool/main/c/ceph/rbd-fuse_0.74-1precise_i386.deb
pool/main/c/ceph/rbd-fuse_0.74-1quantal_amd64.deb
pool/main/c/ceph/rbd-fuse_0.74-1quantal_armhf.deb
pool/main/c/ceph/rbd-fuse_0.74-1quantal_i386.deb
pool/main/c/ceph/rbd-fuse_0.74-1raring_amd64.deb
pool/main/c/ceph/rbd-fuse_0.74-1raring_armhf.deb
pool/main/c/ceph/rbd-fuse_0.74-1raring_i386.deb
pool/main/c/ceph/rbd-fuse_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/rbd-fuse_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/rbd-fuse_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/rbd-fuse_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1precise_amd64.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1precise_i386.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1quantal_amd64.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1quantal_armhf.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1quantal_i386.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1raring_amd64.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1raring_armhf.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1raring_i386.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/rest-bench-dbg_0.74-1~bpo70+1_i386.deb
pool/main/c/ceph/rest-bench_0.74-1precise_amd64.deb
pool/main/c/ceph/rest-bench_0.74-1precise_i386.deb
pool/main/c/ceph/rest-bench_0.74-1quantal_amd64.deb
pool/main/c/ceph/rest-bench_0.74-1quantal_armhf.deb
pool/main/c/ceph/rest-bench_0.74-1quantal_i386.deb
pool/main/c/ceph/rest-bench_0.74-1raring_amd64.deb
pool/main/c/ceph/rest-bench_0.74-1raring_armhf.deb
pool/main/c/ceph/rest-bench_0.74-1raring_i386.deb
pool/main/c/ceph/rest-bench_0.74-1~bpo60+1_amd64.deb
pool/main/c/ceph/rest-bench_0.74-1~bpo60+1_i386.deb
pool/main/c/ceph/rest-bench_0.74-1~bpo70+1_amd64.deb
pool/main/c/ceph/rest-bench_0.74-1~bpo70+1_i386.deb

sent 7685162803 bytes  received 29185 bytes  35996215.40 bytes/sec
total size is 148723825016  speedup is 19.35
***  syncing testing ***

sending incremental file list
el6/SRPMS/
el6/SRPMS/ceph-0.74-0.el6.src.rpm
el6/SRPMS/repodata/
el6/SRPMS/repodata/filelists.xml.gz
el6/SRPMS/repodata/other.xml.gz
el6/SRPMS/repodata/primary.xml.gz
el6/SRPMS/repodata/repomd.xml
el6/SRPMS/repodata/repomd.xml.asc
el6/noarch/
el6/noarch/repodata/
el6/noarch/repodata/filelists.xml.gz
el6/noarch/repodata/other.xml.gz
el6/noarch/repodata/primary.xml.gz
el6/noarch/repodata/repomd.xml
el6/noarch/repodata/repomd.xml.asc
el6/x86_64/
el6/x86_64/ceph-0.74-0.el6.x86_64.rpm
el6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm
el6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm
el6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm
el6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm
el6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm
el6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm
el6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm
el6/x86_64/librados2-0.74-0.el6.x86_64.rpm
el6/x86_64/librbd1-0.74-0.el6.x86_64.rpm
el6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm
el6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm
el6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm
el6/x86_64/repodata/
el6/x86_64/repodata/filelists.xml.gz
el6/x86_64/repodata/other.xml.gz
el6/x86_64/repodata/primary.xml.gz
el6/x86_64/repodata/repomd.xml
el6/x86_64/repodata/repomd.xml.asc
fc17/SRPMS/
fc17/SRPMS/repodata/
fc17/SRPMS/repodata/filelists.xml.gz
fc17/SRPMS/repodata/other.xml.gz
fc17/SRPMS/repodata/primary.xml.gz
fc17/SRPMS/repodata/repomd.xml
fc17/SRPMS/repodata/repomd.xml.asc
fc17/noarch/
fc17/noarch/repodata/
fc17/noarch/repodata/filelists.xml.gz
fc17/noarch/repodata/other.xml.gz
fc17/noarch/repodata/primary.xml.gz
fc17/noarch/repodata/repomd.xml
fc17/noarch/repodata/repomd.xml.asc
fc17/x86_64/
fc17/x86_64/repodata/
fc17/x86_64/repodata/filelists.xml.gz
fc17/x86_64/repodata/other.xml.gz
fc17/x86_64/repodata/primary.xml.gz
fc17/x86_64/repodata/repomd.xml
fc17/x86_64/repodata/repomd.xml.asc
fc18/SRPMS/
fc18/SRPMS/ceph-0.74-0.fc18.src.rpm
fc18/SRPMS/repodata/
fc18/SRPMS/repodata/filelists.xml.gz
fc18/SRPMS/repodata/other.xml.gz
fc18/SRPMS/repodata/primary.xml.gz
fc18/SRPMS/repodata/repomd.xml
fc18/SRPMS/repodata/repomd.xml.asc
fc18/noarch/
fc18/noarch/repodata/
fc18/noarch/repodata/filelists.xml.gz
fc18/noarch/repodata/other.xml.gz
fc18/noarch/repodata/primary.xml.gz
fc18/noarch/repodata/repomd.xml
fc18/noarch/repodata/repomd.xml.asc
fc18/x86_64/
fc18/x86_64/ceph-0.74-0.fc18.x86_64.rpm
fc18/x86_64/ceph-devel-0.74-0.fc18.x86_64.rpm
fc18/x86_64/ceph-fuse-0.74-0.fc18.x86_64.rpm
fc18/x86_64/ceph-radosgw-0.74-0.fc18.x86_64.rpm
fc18/x86_64/ceph-test-0.74-0.fc18.x86_64.rpm
fc18/x86_64/cephfs-java-0.74-0.fc18.x86_64.rpm
fc18/x86_64/libcephfs1-0.74-0.fc18.x86_64.rpm
fc18/x86_64/libcephfs_jni1-0.74-0.fc18.x86_64.rpm
fc18/x86_64/librados2-0.74-0.fc18.x86_64.rpm
fc18/x86_64/librbd1-0.74-0.fc18.x86_64.rpm
fc18/x86_64/python-ceph-0.74-0.fc18.x86_64.rpm
fc18/x86_64/rbd-fuse-0.74-0.fc18.x86_64.rpm
fc18/x86_64/rest-bench-0.74-0.fc18.x86_64.rpm
fc18/x86_64/repodata/
fc18/x86_64/repodata/filelists.xml.gz
fc18/x86_64/repodata/other.xml.gz
fc18/x86_64/repodata/primary.xml.gz
fc18/x86_64/repodata/repomd.xml
fc18/x86_64/repodata/repomd.xml.asc
fc19/SRPMS/
fc19/SRPMS/ceph-0.74-0.fc19.src.rpm
fc19/SRPMS/repodata/
fc19/SRPMS/repodata/filelists.xml.gz
fc19/SRPMS/repodata/other.xml.gz
fc19/SRPMS/repodata/primary.xml.gz
fc19/SRPMS/repodata/repomd.xml
fc19/SRPMS/repodata/repomd.xml.asc
fc19/noarch/
fc19/noarch/repodata/
fc19/noarch/repodata/filelists.xml.gz
fc19/noarch/repodata/other.xml.gz
fc19/noarch/repodata/primary.xml.gz
fc19/noarch/repodata/repomd.xml
fc19/noarch/repodata/repomd.xml.asc
fc19/x86_64/
fc19/x86_64/ceph-0.74-0.fc19.x86_64.rpm
fc19/x86_64/ceph-devel-0.74-0.fc19.x86_64.rpm
fc19/x86_64/ceph-fuse-0.74-0.fc19.x86_64.rpm
fc19/x86_64/ceph-radosgw-0.74-0.fc19.x86_64.rpm
fc19/x86_64/ceph-test-0.74-0.fc19.x86_64.rpm
fc19/x86_64/cephfs-java-0.74-0.fc19.x86_64.rpm
fc19/x86_64/libcephfs1-0.74-0.fc19.x86_64.rpm
fc19/x86_64/libcephfs_jni1-0.74-0.fc19.x86_64.rpm
fc19/x86_64/librados2-0.74-0.fc19.x86_64.rpm
fc19/x86_64/librbd1-0.74-0.fc19.x86_64.rpm
fc19/x86_64/python-ceph-0.74-0.fc19.x86_64.rpm
fc19/x86_64/rbd-fuse-0.74-0.fc19.x86_64.rpm
fc19/x86_64/rest-bench-0.74-0.fc19.x86_64.rpm
fc19/x86_64/repodata/
fc19/x86_64/repodata/filelists.xml.gz
fc19/x86_64/repodata/other.xml.gz
fc19/x86_64/repodata/primary.xml.gz
fc19/x86_64/repodata/repomd.xml
fc19/x86_64/repodata/repomd.xml.asc
opensuse12.2/SRPMS/
opensuse12.2/SRPMS/ceph-0.74-0.src.rpm
opensuse12.2/SRPMS/repodata/
opensuse12.2/SRPMS/repodata/filelists.xml.gz
opensuse12.2/SRPMS/repodata/other.xml.gz
opensuse12.2/SRPMS/repodata/primary.xml.gz
opensuse12.2/SRPMS/repodata/repomd.xml
opensuse12.2/SRPMS/repodata/repomd.xml.asc
opensuse12.2/noarch/
opensuse12.2/noarch/repodata/
opensuse12.2/noarch/repodata/filelists.xml.gz
opensuse12.2/noarch/repodata/other.xml.gz
opensuse12.2/noarch/repodata/primary.xml.gz
opensuse12.2/noarch/repodata/repomd.xml
opensuse12.2/noarch/repodata/repomd.xml.asc
opensuse12.2/x86_64/
opensuse12.2/x86_64/ceph-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-debugsource-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-devel-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-fuse-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-fuse-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-radosgw-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-radosgw-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-test-0.74-0.x86_64.rpm
opensuse12.2/x86_64/ceph-test-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/cephfs-java-0.74-0.x86_64.rpm
opensuse12.2/x86_64/libcephfs1-0.74-0.x86_64.rpm
opensuse12.2/x86_64/libcephfs1-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm
opensuse12.2/x86_64/libcephfs_jni1-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/librados2-0.74-0.x86_64.rpm
opensuse12.2/x86_64/librados2-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/librbd1-0.74-0.x86_64.rpm
opensuse12.2/x86_64/librbd1-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/python-ceph-0.74-0.x86_64.rpm
opensuse12.2/x86_64/rbd-fuse-0.74-0.x86_64.rpm
opensuse12.2/x86_64/rbd-fuse-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/rest-bench-0.74-0.x86_64.rpm
opensuse12.2/x86_64/rest-bench-debuginfo-0.74-0.x86_64.rpm
opensuse12.2/x86_64/repodata/
opensuse12.2/x86_64/repodata/filelists.xml.gz
opensuse12.2/x86_64/repodata/other.xml.gz
opensuse12.2/x86_64/repodata/primary.xml.gz
opensuse12.2/x86_64/repodata/repomd.xml
opensuse12.2/x86_64/repodata/repomd.xml.asc
opensuse12/x86_64/
opensuse12/x86_64/repodata/
opensuse12/x86_64/repodata/filelists.xml.gz
opensuse12/x86_64/repodata/other.xml.gz
opensuse12/x86_64/repodata/primary.xml.gz
opensuse12/x86_64/repodata/repomd.xml
opensuse12/x86_64/repodata/repomd.xml.asc
rhel6/SRPMS/
rhel6/SRPMS/ceph-0.74-0.el6.src.rpm
rhel6/SRPMS/repodata/
rhel6/SRPMS/repodata/filelists.xml.gz
rhel6/SRPMS/repodata/other.xml.gz
rhel6/SRPMS/repodata/primary.xml.gz
rhel6/SRPMS/repodata/repomd.xml
rhel6/SRPMS/repodata/repomd.xml.asc
rhel6/noarch/
rhel6/noarch/repodata/
rhel6/noarch/repodata/filelists.xml.gz
rhel6/noarch/repodata/other.xml.gz
rhel6/noarch/repodata/primary.xml.gz
rhel6/noarch/repodata/repomd.xml
rhel6/noarch/repodata/repomd.xml.asc
rhel6/x86_64/
rhel6/x86_64/ceph-0.74-0.el6.x86_64.rpm
rhel6/x86_64/ceph-debuginfo-0.74-0.el6.x86_64.rpm
rhel6/x86_64/ceph-devel-0.74-0.el6.x86_64.rpm
rhel6/x86_64/ceph-fuse-0.74-0.el6.x86_64.rpm
rhel6/x86_64/ceph-radosgw-0.74-0.el6.x86_64.rpm
rhel6/x86_64/ceph-test-0.74-0.el6.x86_64.rpm
rhel6/x86_64/cephfs-java-0.74-0.el6.x86_64.rpm
rhel6/x86_64/libcephfs1-0.74-0.el6.x86_64.rpm
rhel6/x86_64/libcephfs_jni1-0.74-0.el6.x86_64.rpm
rhel6/x86_64/librados2-0.74-0.el6.x86_64.rpm
rhel6/x86_64/librbd1-0.74-0.el6.x86_64.rpm
rhel6/x86_64/python-ceph-0.74-0.el6.x86_64.rpm
rhel6/x86_64/rbd-fuse-0.74-0.el6.x86_64.rpm
rhel6/x86_64/rest-bench-0.74-0.el6.x86_64.rpm
rhel6/x86_64/repodata/
rhel6/x86_64/repodata/filelists.xml.gz
rhel6/x86_64/repodata/other.xml.gz
rhel6/x86_64/repodata/primary.xml.gz
rhel6/x86_64/repodata/repomd.xml
rhel6/x86_64/repodata/repomd.xml.asc
sles11/SRPMS/
sles11/SRPMS/ceph-0.74-0.src.rpm
sles11/SRPMS/repodata/
sles11/SRPMS/repodata/filelists.xml.gz
sles11/SRPMS/repodata/other.xml.gz
sles11/SRPMS/repodata/primary.xml.gz
sles11/SRPMS/repodata/repomd.xml
sles11/SRPMS/repodata/repomd.xml.asc
sles11/noarch/
sles11/noarch/repodata/
sles11/noarch/repodata/filelists.xml.gz
sles11/noarch/repodata/other.xml.gz
sles11/noarch/repodata/primary.xml.gz
sles11/noarch/repodata/repomd.xml
sles11/noarch/repodata/repomd.xml.asc
sles11/x86_64/
sles11/x86_64/ceph-0.74-0.x86_64.rpm
sles11/x86_64/ceph-debuginfo-0.74-0.x86_64.rpm
sles11/x86_64/ceph-debugsource-0.74-0.x86_64.rpm
sles11/x86_64/ceph-devel-0.74-0.x86_64.rpm
sles11/x86_64/ceph-fuse-0.74-0.x86_64.rpm
sles11/x86_64/ceph-radosgw-0.74-0.x86_64.rpm
sles11/x86_64/ceph-test-0.74-0.x86_64.rpm
sles11/x86_64/cephfs-java-0.74-0.x86_64.rpm
sles11/x86_64/libcephfs1-0.74-0.x86_64.rpm
sles11/x86_64/libcephfs_jni1-0.74-0.x86_64.rpm
sles11/x86_64/librados2-0.74-0.x86_64.rpm
sles11/x86_64/librbd1-0.74-0.x86_64.rpm
sles11/x86_64/python-ceph-0.74-0.x86_64.rpm
sles11/x86_64/rbd-fuse-0.74-0.x86_64.rpm
sles11/x86_64/rest-bench-0.74-0.x86_64.rpm
sles11/x86_64/repodata/
sles11/x86_64/repodata/filelists.xml.gz
sles11/x86_64/repodata/other.xml.gz
sles11/x86_64/repodata/primary.xml.gz
sles11/x86_64/repodata/repomd.xml
sles11/x86_64/repodata/repomd.xml.asc

sent 1507213200 bytes  received 17990 bytes  36318823.86 bytes/sec
total size is 28047793503  speedup is 18.61
ubuntu@jenkins: ~ubuntu@jenkins:~$ exit
exit

Script done on Wed 01 Jan 2014 06:35:03 AM UTC

Log 3:  Build has finished, push git updates to github

Script started on Wed 01 Jan 2014 07:51:26 AM UTC

glowell@jenkins: ~/build/ceph-0.74/ceph$ git remote add -f gh git@github.com:ceph/ceph.git
Updating gh
remote: Counting objects: 725, done.K
remote: Compressing objects: 100% (349/349),Kdone.K
remote:nTotale725:(delta(515),2reused 521 (delta 372)K
Receiving objects: 100% (725/725), 782.93 KiB, done.
Resolving deltas: 100% (515/515), done.
From github.com:ceph/ceph
 * [new branch]      argonaut -> gh/argonaut
 
 <list of branchs ... >
 * [new branch]      wip_observer -> gh/wip_observer

glowell@jenkins: ~/build/ceph-0.74/ceph$ git remote -v
gh git@github.com:ceph/ceph.git (fetch)
gh git@github.com:ceph/ceph.git (push)
jenkins git@jenkins:ceph/ceph (fetch)
jenkins git@jenkins:ceph/ceph (push)

glowell@jenkins: ~/build/ceph-0.74/ceph$ git push gh next
Counting objects: 9, done.
Compressing objects: 100% (5/5), done.
Writing objects: 100% (5/5), 516 bytes, done.
Total 5 (delta 4), reused 0 (delta 0)
To git@github.com:ceph/ceph.git
   4f07848..c165483  next -> next

glowell@jenkins: ~/build/ceph-0.74/ceph$ git push gh v0.74
Counting objects: 1, done.
Writing objects: 100% (1/1)
Writing objects: 100% (1/1), 802 bytes, done.
Total 1 (delta 0), reused 0 (delta 0)
To git@github.com:ceph/ceph.git^
 * [new tag]         v0.74 -> v0.74


if *not* doing a stable release, then:

    glowell@jenkins: ~/build/ceph-0.74/ceph$ git push gh HEAD:last
    Total 0 (delta 0), reused 0 (delta 0)
    To git@github.com:ceph/ceph.git
       d8ad51e..c165483  HEAD -> last

glowell@jenkins: ~/build/ceph-0.74/ceph$ git checkout master
Switched to branch 'master'

glowell@jenkins: ~/build/ceph-0.74/ceph$ git pull
From jenkins:ceph/ceph
   87db89e..cae663a  master -> jenkins/master
   1abb169..2354b95  dumpling -> jenkins/dumpling
   2f25bfe..f75c973  emperor -> jenkins/emperor
 + c165483...4f07848 next -> jenkins/next  (forced update)
   76ad85d..b4fc16c  port/misc -> jenkins/port/misc
 * [new branch]      wip-6914 -> jenkins/wip-6914
 + 3db1682...571eb9a wip-agent -> jenkins/wip-agent  (forced update)
 + 281e67b...9f852fe wip-cache-snap -> jenkins/wip-cache-snap  (forced update)
 * [new branch]      wip-client-io-rebased-2 -> jenkins/wip-client-io-rebased-2
 * [new branch]      wip-empty-rbd-ls -> jenkins/wip-empty-rbd-ls
 * [new branch]      wip-listomapvals -> jenkins/wip-listomapvals
 + e05049e...d291267 wip-omapclear -> jenkins/wip-omapclear  (forced update)
Updating 87db89e..cae663a
Fast-forward
 qa/workunits/rados/test_rados_tool.sh     | 24 +++++++++++++++++
 src/Makefile-env.am     |  7 +++--
 src/ceph_mon.cc     |  122 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++--------
 src/common/common_init.cc     |  6 +++--
 src/common/common_init.h     |  2 +-
 src/json_spirit/Makefile.am     |  3 +--
 src/librados/Makefile.am     |  5 +++-
 src/librbd/Makefile.am      |  5 +++-
 src/msg/Accepter.cc     |  2 --
 src/msg/SimpleMessenger.cc     |  4 +--
 src/os/Makefile.am     | 11 ++++++--
 src/osd/ErasureCodePluginJerasure/Makefile.am     |  5 +++-
 src/osd/ReplicatedPG.cc     |  4 +--
 src/osd/osd_types.cc     |  3 +++
 src/rbd.cc     | 29 ++++++++++++++++-----
 src/test/cls_rbd/test_cls_rbd.cc     | 14 +++++-----
 src/test/mon/osd-pool-create.sh     |  2 +-
 src/test/osd/ErasureCodePluginMissingEntryPoint.cc |  3 +++
 src/tools/rados/rados.cc     |  2 ++
 19 files changed, 210 insertions(+), 43 deletions(-)
 create mode 100755 qa/workunits/rados/test_rados_tool.sh

glowell@jenkins: ~/build/ceph-0.74/ceph$ git merge next
Auto-merging configure.ac
### default merge message: "Merge branch 'next'"

Merge made by the 'recursive' strategy.
 configure.ac        |    2 +-
 debian/changelog      |    6 ++++++
 src/rgw/rgw_bucket.cc |   16 ++++++++++++----
 3 files changed, 19 insertions(+), 5 deletions(-)

glowell@jenkins: ~/build/ceph-0.74/ceph$ git push gh master
Counting objects: 16, done.
Compressing objects: 100% (6/6), done.
Writing objects: 100% (6/6), 592 bytes, done.
Total 6 (delta 5), reused 0 (delta 0)
To git@github.com:ceph/ceph.git
   cae663a..fe3fd5f  master -> master

glowell@jenkins: ~/build/ceph-0.74/ceph$ git push gh HEAD:next
Total 0 (delta 0), reused 0 (delta 0)
To git@github.com:ceph/ceph.git
   c165483..fe3fd5f  HEAD -> next

Script done on Sat 04 Jan 2014 01:33:20 AM UTC
glowell@jenkins:~/build/ceph-0.74/logs$ 
