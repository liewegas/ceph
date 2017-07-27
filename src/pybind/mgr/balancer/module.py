
"""
Balance PG distribution across OSDs.
"""

import errno
import json
import random
import time
from mgr_module import MgrModule, CommandResult
from threading import Event

# available modes: 'none', 'crush', 'crush-compat', 'upmap', 'osd_weight'
default_mode = 'none'
default_sleep_interval = 60   # seconds
default_max_misplaced = .03   # max ratio of pgs replaced at a time

class Plan:
    def __init__(self, name, osdmap):
        self.mode = 'unknown'
        self.name = name
        self.osdmap = osdmap
        self.crush = osdmap.get_crush()
        self.crush_dump = self.crush.dump()
        self.osd_weights = {}
        self.compat_ws = {}
        self.inc = osdmap.new_incremental()

    def get_osdmap(self):
        return self.osdmap

    def apply(self):
        inc.set_osd_reweights(self.osd_weights)
        inc.set_crush_compat_weight_set_weights(self.compat_ws)
        return osdmap.apply_incremental(inc)

    def dump(self):
        return json.dumps(self.inc.dump(), indent=4)

    def show(self):
        ls = []
        ls.append('# starting osdmap epoch %d' % self.osdmap.get_epoch())
        ls.append('# starting crush version %d' % self.osdmap.get_crush_version())
        ls.append('# mode %s' % self.mode)
        if len(self.compat_ws) and \
           '-1' in self.crush_dump.get('choose_args', {}):
            ls.append('ceph osd crush weight-set create-compat')
        for osd, weight in self.compat_ws.iteritems():
            ls.append('ceph osd crush weight-set reweight-compat %s %f' %
                      (osd, weight))
        for osd, weight in self.osd_weights.iteritems():
            ls.append('ceph osd reweight osd.%d %f' % (osd, weight))
        incdump = self.inc.dump()
        for pgid in incdump.get('old_pg_upmap_items', []):
            ls.append('ceph osd rm-pg-upmap-items %s' % pgid)
        for item in incdump.get('new_pg_upmap_items', []):
            osdlist = []
            for m in item['mappings']:
                osdlist += [m['from'], m['to']]
            ls.append('ceph osd pg-upmap-items %s %s' %
                      (item['pgid'], ' '.join([str(a) for a in osdlist])))
        return '\n'.join(ls)



class Module(MgrModule):
    COMMANDS = [
        {
            "cmd": "balancer status",
            "desc": "Show balancer status",
            "perm": "r",
        },
        {
            "cmd": "balancer mode name=mode,type=CephChoices,strings=none|crush-compat|upmap",
            "desc": "Set balancer mode",
            "perm": "rw",
        },
        {
            "cmd": "balancer on",
            "desc": "Enable automatic balancing",
            "perm": "rw",
        },
        {
            "cmd": "balancer off",
            "desc": "Disable automatic balancing",
            "perm": "rw",
        },
        {
            "cmd": "balancer optimize name=plan,type=CephString",
            "desc": "Run optimizer to create a new plan",
            "perm": "rw",
        },
        {
            "cmd": "balancer show name=plan,type=CephString",
            "desc": "Show details of an optimization plan",
            "perm": "r",
        },
        {
            "cmd": "balancer rm name=plan,type=CephString",
            "desc": "Discard an optimization plan",
            "perm": "rw",
        },
        {
            "cmd": "balancer reset",
            "desc": "Discard all optimization plans",
            "perm": "rw",
        },
        {
            "cmd": "balancer dump name=plan,type=CephString",
            "desc": "Show an optimization plan",
            "perm": "r",
        },
        {
            "cmd": "balancer execute name=plan,type=CephString",
            "desc": "Execute an optimization plan",
            "perm": "r",
        },
    ]
    active = False
    run = True
    plans = {}
    mode = ''

    def __init__(self, *args, **kwargs):
        super(Module, self).__init__(*args, **kwargs)
        self.event = Event()

    def handle_command(self, command):
        self.log.warn("Handling command: '%s'" % str(command))
        if command['prefix'] == 'balancer status':
            s = {
                'plans': self.plans.keys(),
                'active': self.active,
                'mode': self.get_config('mode', default_mode),
            }
            return (0, json.dumps(s, indent=4), '')
        elif command['prefix'] == 'balancer mode':
            self.set_config('mode', command['mode'])
            return (0, '', '')
        elif command['prefix'] == 'balancer on':
            if not self.active:
                self.set_config('active', '1')
                self.active = True
            self.event.set()
            return (0, '', '')
        elif command['prefix'] == 'balancer off':
            if self.active:
                self.set_config('active', '')
                self.active = False
            self.event.set()
            return (0, '', '')
        elif command['prefix'] == 'balancer optimize':
            plan = self.plan_create(command['plan'])
            self.optimize(plan)
            return (0, '', '')
        elif command['prefix'] == 'balancer rm':
            self.plan_rm(command['name'])
            return (0, '', '')
        elif command['prefix'] == 'balancer reset':
            self.plans = {}
            return (0, '', '')
        elif command['prefix'] == 'balancer dump':
            plan = self.plans.get(command['plan'])
            if not plan:
                return (-errno.ENOENT, '', 'plan %s not found' % command['plan'])
            return (0, plan.dump(), '')
        elif command['prefix'] == 'balancer show':
            plan = self.plans.get(command['plan'])
            if not plan:
                return (-errno.ENOENT, '', 'plan %s not found' % command['plan'])
            return (0, plan.show(), '')
        elif command['prefix'] == 'balancer execute':
            plan = self.plans.get(command['plan'])
            if not plan:
                return (-errno.ENOENT, '', 'plan %s not found' % command['plan'])
            self.execute(plan)
            self.plan_rm(plan)
            return (0, '', '')
        else:
            return (-errno.EINVAL, '',
                    "Command not found '{0}'".format(command['prefix']))

    def shutdown(self):
        self.log.info('Stopping')
        self.run = False
        self.event.set()

    def serve(self):
        self.log.info('Starting')
        while self.run:
            self.log.debug('Waking up')
            self.active = self.get_config('active', '') is not ''
            sleep_interval = float(self.get_config('sleep_interval',
                                                   default_sleep_interval))
            if self.active:
                self.log.debug('Running')
                plan = self.plan_create('auto-foo')
                self.optimize(plan)
                #self.plan_apply(plan)
            self.log.debug('Sleeping for %d', sleep_interval)
            self.event.wait(sleep_interval)
            self.event.clear()

    def plan_create(self, name):
        plan = Plan(name, self.get_osdmap())
        self.plans[name] = plan
        return plan

    def plan_rm(self, name):
        if name in self.plans:
            del self.plans[name]

    def optimize(self, plan):
        self.log.info('Optimize plan %s' % plan.name)
        plan.mode = self.get_config('mode', default_mode)
        max_misplaced = float(self.get_config('max_misplaced',
                                              default_max_misplaced))
        self.log.info('Mode %s, max misplaced %f' %
                      (plan.mode, max_misplaced))

        info = self.get('pg_status')
        unknown = info.get('unknown_pgs_ratio', 0.0)
        degraded = info.get('degraded_ratio', 0.0)
        inactive = info.get('inactive_pgs_ratio', 0.0)
        misplaced = info.get('misplaced_ratio', 0.0)
        self.log.debug('unknown %f degraded %f inactive %f misplaced %g',
                       unknown, degraded, inactive, misplaced)
        if unknown > 0.0:
            self.log.info('Some PGs (%f) are unknown; waiting', unknown)
        elif degraded > 0.0:
            self.log.info('Some PGs (%f) are degraded; waiting', degraded)
        elif inactive > 0.0:
            self.log.info('Some PGs (%f) are inactive; waiting', inactive)
        elif misplaced >= max_misplaced:
            self.log.info('Too many PGs (%f > %f) are misplaced; waiting',
                          misplaced, max_misplaced)
        else:
            if plan.mode == 'upmap':
                self.do_upmap(plan)
            elif plan.mode == 'crush':
                self.do_crush()
            elif plan.mode == 'crush-compat':
                self.do_crush_compat()
            elif plan.mode == 'osd_weight':
                self.osd_weight()
            elif plan.mode == 'none':
                self.log.info('Idle')
            else:
                self.log.info('Unrecognized mode %s' % plan.mode)

    def do_upmap(self, plan):
        self.log.info('do_upmap')
        max_iterations = self.get_config('upmap_max_iterations', 10)
        max_deviation = self.get_config('upmap_max_deviation', .01)

        osdmap = plan.get_osdmap()
        osdmap_dump = osdmap.dump()
        pools = [str(i['pool_name']) for i in osdmap_dump.get('pools',[])]
        if len(pools) == 0:
            self.log.info('no pools, nothing to do')
            return
        # shuffle pool list so they all get equal (in)attention
        random.shuffle(pools)
        self.log.info('pools %s' % pools)

        inc = plan.inc
        total_did = 0
        left = max_iterations
        for pool in pools:
            did = osdmap.calc_pg_upmaps(inc, max_deviation, left, [pool])
            total_did += did
            left -= did
            if left <= 0:
                break
        self.log.info('prepared %d/%d changes' % (total_did, max_iterations))


    def do_crush_compat(self):
        self.log.info('do_crush_compat')
        osdmap = self.get_osdmap()
        crush = osdmap.get_crush()

        weight_set = self.get_compat_weight_set_weights()

        # get subtree weight maps, check for overlap
        roots = crush.find_takes()
        self.log.debug('roots %s', roots)
        weight_maps = {}
        visited = {}
        overlap = {}
        for root in roots:
            weight_maps[root] = crush.get_take_weight_osd_map(root)
            self.log.debug(' map for %d: %s' % (root, weight_maps[root]))
            for osd in weight_maps[root].iterkeys():
                if osd in visited:
                    overlap[osd] = 1
                visited[osd] = 1
        if len(overlap) > 0:
            self.log.err('error: some osds belong to multiple subtrees: %s' %
                         overlap)
            return

        # select a cost mode: pg, pgsize, or utilization
        # FIXME: when we add utilization support, we need to throttle back
        # so that we don't run if *any* objects are misplaced.  with 'pgs' we
        # can look at up mappings, which lets us look ahead a bit.
        cost_mode = 'pg'

        # go
        random.shuffle(roots)
        for root in roots:
            pools = osdmap.get_pools_by_take(root)
            self.log.info('Balancing root %s pools %s' % (root, pools))
            wm = weight_maps[root]
            util = self.get_util(cost_mode, pools, wm.keys())
            self.log.info('wm %s util %s' % (wm, util))
            if len(util) == 0:
                self.log.info('no utilization information, stopping')
                return
            target = self.get_target(util)
            self.log.info('target %s' % target)
            if target == 0:
                continue

            queue = sorted(util, key=lambda osd: -abs(target - util[osd]))
            self.log.info('queue %s' % queue)

            for osd in queue:
                deviation = float(util[osd]) - float(target)
                if deviation == 0:
                    break
                self.log.debug('osd.%d deviation %f', osd, deviation)
                weight = weight_set[osd]
                calc_weight = (float(target) / float(util[osd])) * weight
                new_weight = weight * .7 + calc_weight * .3
                self.log.debug('Reweight osd.%d %f -> %f', osd, weight,
                               new_weight)
                self.compat_weight_set_reweight(osd, new_weight)

    def compat_weight_set_reweight(self, osd, new_weight):
        self.log.debug('ceph osd crush weight-set reweight-compat')
        result = CommandResult('')
        self.send_command(result, 'mon', '', json.dumps({
            'prefix': 'osd crush weight-set reweight-compat',
            'format': 'json',
            'item': 'osd.%d' % osd,
            'weight': [new_weight],
        }), '')
        r, outb, outs = result.wait()
        if r != 0:
            self.log.error('Error setting compat weight-set osd.%d to %f' %
            (osd, new_weight))
            return

    def get_compat_weight_set_weights(self):
        # enable compat weight-set
        self.log.debug('ceph osd crush weight-set create-compat')
        result = CommandResult('')
        self.send_command(result, 'mon', '', json.dumps({
            'prefix': 'osd crush weight-set create-compat',
            'format': 'json',
        }), '')
        r, outb, outs = result.wait()
        if r != 0:
            self.log.error('Error creating compat weight-set')
            return

        result = CommandResult('')
        self.send_command(result, 'mon', '', json.dumps({
            'prefix': 'osd crush dump',
            'format': 'json',
        }), '')
        r, outb, outs = result.wait()
        if r != 0:
            self.log.error('Error dumping crush map')
            return
        try:
            crushmap = json.loads(outb)
        except:
            raise RuntimeError('unable to parse crush map')

        raw = crushmap.get('choose_args',{}).get('-1', [])
        weight_set = {}
        for b in raw:
            bucket = None
            for t in crushmap['buckets']:
                if t['id'] == b['bucket_id']:
                    bucket = t
                    break
            if not bucket:
                raise RuntimeError('could not find bucket %s' % b['bucket_id'])
            self.log.debug('bucket items %s' % bucket['items'])
            self.log.debug('weight set %s' % b['weight_set'][0])
            if len(bucket['items']) != len(b['weight_set'][0]):
                raise RuntimeError('weight-set size does not match bucket items')
            for pos in range(len(bucket['items'])):
                weight_set[bucket['items'][pos]['id']] = b['weight_set'][0][pos]

        self.log.debug('weight_set weights %s' % weight_set)
        return weight_set

    def get_util(self, cost_mode, pools, osds):
        if cost_mode == 'pg' or \
           cost_mode == 'pg_bytes' or \
           cost_mode == 'pg_objects':
            util_map = {}
            for osd in osds:
                util_map[osd] = 0
            dump = self.get('pg_dump')
            #self.log.info('dump %s' % dump)
            self.log.info('osds %s' % osds)
            for pg in dump['pg_stats']:
                inpool = False
                for pool in pools:
                    if pg['pgid'].startswith(str(pool) + '.'):
                        inpool = True
                        break
                if not inpool:
                    self.log.info('skipping %s' % pg['pgid'])
                    continue
                self.log.info('pg %s osds %s' % (pg['pgid'], pg['up']))
                for osd in [int(a) for a in pg['up']]:
                    if osd in osds:
                        if cost_mode == 'pg':
                            util_map[osd] += 1
                        elif cost_mode == 'pg_bytes':
                            util_map[osd] += pg['stat_sum']['num_bytes']
                        elif cost_mode == 'pg_objects':
                            util_map[osd] += pg['stat_sum']['num_objects']
            return util_map
        else:
            raise RuntimeError('unsupported cost mode %s' % cost_mode)

    def get_target(self, util_map):
        total = 0
        count = 0
        for k, v in util_map.iteritems():
            total += v;
            count += 1
        return total / count

    def do_crush(self):
        self.log.info('do_crush (not yet implemented)')

    def do_osd_weight(self):
        self.log.info('do_osd_weight (not yet implemented)')

    def execute(self, plan):
        self.log.info('Executing plan %s' % plan.name)

        commands = []

        # compat weight-set
        if len(plan.compat_ws) and \
           '-1' in plan.crush_dump.get('choose_args', {}):
            self.log.debug('ceph osd crush weight-set create-compat')
            result = CommandResult('')
            self.send_command(result, 'mon', '', json.dumps({
                'prefix': 'osd crush weight-set create-compat',
                'format': 'json',
            }), '')
            r, outb, outs = result.wait()
            if r != 0:
                self.log.error('Error creating compat weight-set')
                return

        for osd, weight in plan.compat_ws.iteritems():
            self.log.info('ceph osd crush weight-set reweight-compat osd.%d %f',
                          osd, weight)
            result = CommandResult('foo')
            self.send_command(result, 'mon', '', json.dumps({
                'prefix': 'osd crush weight-set reweight-compat',
                'format': 'json',
                'item': 'osd.%d' % osd,
                'weight': [weight],
            }), 'foo')
            commands.append(result)

        # new_weight
        reweightn = {}
        for osd, weight in plan.osd_weights.iteritems():
            reweightn[int(osd)] = float(weight) / float(0x10000)
        if len(reweightn):
            self.log.info('ceph osd reweightn %s', reweightn)
            result = CommandResult('foo')
            self.send_command(result, 'mon', '', json.dumps({
                'prefix': 'osd reweightn',
                'format': 'json',
                'weights': json.dumps(reweightn),
            }), 'foo')
            commands.append(result)

        # upmap
        incdump = plan.inc.dump()
        for pgid in incdump.get('old_pg_upmap_items', []):
            self.log.info('ceph osd rm-pg-upmap-items %s', pgid)
            result = CommandResult('foo')
            self.send_command(result, 'mon', '', json.dumps({
                'prefix': 'osd rm-pg-upmap-items',
                'format': 'json',
                'pgid': pgid,
            }), 'foo')
            commands.append(result)

        for item in incdump.get('new_pg_upmap_items', []):
            self.log.info('ceph osd pg-upmap-items %s mappings %s', item['pgid'],
                          item['mappings'])
            osdlist = []
            for m in item['mappings']:
                osdlist += [m['from'], m['to']]
            result = CommandResult('foo')
            self.send_command(result, 'mon', '', json.dumps({
                'prefix': 'osd pg-upmap-items',
                'format': 'json',
                'pgid': item['pgid'],
                'id': osdlist,
            }), 'foo')
            commands.append(result)

        # wait for commands
        for result in commands:
            r, outb, outs = result.wait()
            if r != 0:
                self.log.error('Error on command')
                return
