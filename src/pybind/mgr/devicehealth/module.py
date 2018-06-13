
"""
Device health monitoring
"""

import errno
import json
from mgr_module import MgrModule, CommandResult
import rados
from threading import Event
from datetime import datetime, timedelta, date, time

TIME_FORMAT = '%Y%m%d-%H%M%S'

class Module(MgrModule):
    OPTIONS = [
        { 'name': 'active' },
        { 'name': 'scrape_frequency' },
        { 'name': 'pool_name' },
        { 'name': 'retention_period' },
    ]
    DEFAULTS = {
        'active': True,
        'scrape_frequency': str(86400),
        'retention_period': str(86400*14),
        'pool_name': 'device_health_metrics',
        'mark_out_threshold': str(86400*14),
        'warn_threshold': str(86400*14*2),
        'self_heal': True,
    }
    active = DEFAULTS['active']
    scrape_frequency = DEFAULTS['scrape_frequency']
    retention_period = DEFAULTS['retention_period']
    pool_name = DEFAULTS['pool_name']

    COMMANDS = [
        {
            "cmd": "device query-daemon-health-metrics "
                   "name=who,type=CephString",
            "desc": "Get device health metrics for a given daemon (OSD)",
            "perm": "r"
        },
        {
            "cmd": "device scrape-daemon-health-metrics "
                   "name=who,type=CephString",
            "desc": "Scrape and store device health metrics for a given daemon",
            "perm": "r"
        },
        {
            "cmd": "device scrape-health-metrics name=devid,type=CephString,req=False",
            "desc": "Scrape and store health metrics",
            "perm": "r"
        },
        {
            "cmd": "device show-health-metrics name=devid,type=CephString name=sample,type=CephString,req=False",
            "desc": "Show stored device metrics for the device",
            "perm": "r"
        },
    ]

    run = True
    event = Event()
    last_scrape_time = ""

    def handle_command(self, cmd):
        self.log.error("handle_command")

        if cmd['prefix'] == 'device query-daemon-health-metrics':
            who = cmd.get('who', '')
            if who[0:4] != 'osd.':
                return (-errno.EINVAL, '', 'not a valid <osd.NNN> id')
            osd_id = who[4:]
            result = CommandResult('')
            self.send_command(result, 'osd', osd_id, json.dumps({
                'prefix': 'smart',
                'format': 'json',
            }), '')
            r, outb, outs = result.wait()
            return (r, outb, outs)
        elif cmd['prefix'] == 'device scrape-daemon-health-metrics':
            who = cmd.get('who', '')
            if who[0:4] != 'osd.':
                return (-errno.EINVAL, '', 'not a valid <osd.NNN> id')
            id = int(who[4:])
            return self.scrape_osd(id)
        elif cmd['prefix'] == 'device scrape-health-metrics':
            if 'devid' in cmd:
                return self.scrape_device(cmd['devid'])
            return self.scrape_all();
        elif cmd['prefix'] == 'device show-health-metrics':
            return self.show_device_metrics(cmd['devid'], cmd.get('sample'))

        else:
            # mgr should respect our self.COMMANDS and not call us for
            # any prefix we don't advertise
            raise NotImplementedError(cmd['prefix'])

    def refresh_config(self):
        self.active = self.get_config('active', '') is not '' or 'false'
        for opt, value in self.DEFAULTS.iteritems():
            setattr(self, opt, self.get_config(opt) or value)

    def serve(self):
        self.log.info("Starting")
        while self.run:
            self.refresh_config()

            # TODO normalize/align sleep interval
            sleep_interval = int(self.scrape_frequency)

            self.log.debug('Sleeping for %d seconds', sleep_interval)
            ret = self.event.wait(sleep_interval)
            self.event.clear()

            # in case 'wait' was interrupted, it could mean config was changed
            # by the user; go back and read config vars
            if ret:
                continue

            self.log.debug('Waking up [%s]',
                           "active" if self.active else "inactive")
            if not self.active:
                continue
            self.log.debug('Running')

    def open_connection(self):
        pools = self.rados.list_pools()
        is_pool = False
        for pool in pools:
            if pool == self.pool_name:
                is_pool = True
                break
        if not is_pool:
            self.rados.create_pool(self.pool_name)
            self.log.debug('create %s pool' % self.pool_name)
        ioctx = self.rados.open_ioctx(self.pool_name)
        return (ioctx)

    def scrape_osd(self, osd_id):
        ioctx = self.open_connection()
        raw_smart_data = self.do_scrape_osd(osd_id, ioctx)
        if raw_smart_data:
            for device, raw_data in raw_smart_data.items():
                data = self.extract_smart_features(raw_data)
                self.put_device_metrics(ioctx, device, data)
        ioctx.close()
        return (0, "", "")

    def scrape_all(self):
        osdmap = self.get("osd_map")
        assert osdmap is not None
        ioctx = self.open_connection()
        did_device = {}
        for osd in osdmap['osds']:
            osd_id = osd['osd']
            raw_smart_data = self.do_scrape_osd(osd_id, ioctx)
            if not raw_smart_data:
                continue
            for device, raw_data in raw_smart_data.items():
                if device in did_device:
                    self.log.debug('skipping duplicate %s' % device)
                    continue
                did_device[device] = 1
                data = self.extract_smart_features(raw_data)
                self.put_device_metrics(ioctx, device, data)

        ioctx.close()
        return (0, "", "")

    def scrape_device(self, devid):
        r = self.get("device " + devid)
        if not r or 'device' not in r.keys():
            return (-errno.ENOENT, '', 'device ' + devid + ' not found')
        daemons = r['device'].get('daemons', [])
        osds = [int(r[4:]) for r in daemons if r.startswith('osd.')]
        if not osds:
            return (-errno.EAGAIN, '',
                    'device ' + devid + ' not claimed by any active OSD daemons')
        osd_id = osds[0]
        ioctx = self.open_connection()
        raw_smart_data = self.do_scrape_osd(osd_id, ioctx, devid=devid)
        if raw_smart_data:
            for device, raw_data in raw_smart_data.items():
                data = self.extract_smart_features(raw_data)
                self.put_device_metrics(ioctx, device, data)
        ioctx.close()
        return (0, "", "")

    def do_scrape_osd(self, osd_id, ioctx, devid=''):
        self.log.debug('do_scrape_osd osd.%d' % osd_id)

        # scrape from osd
        result = CommandResult('')
        self.send_command(result, 'osd', str(osd_id), json.dumps({
            'prefix': 'smart',
            'format': 'json',
            'devid': devid,
        }), '')
        r, outb, outs = result.wait()

        try:
            return json.loads(outb)
        except:
            self.log.debug('Fail to parse JSON result from "%s"' % outb)

    def put_device_metrics(self, ioctx, devid, data):
        old_key = datetime.now() - timedelta(
            seconds=int(self.retention_period))
        prune = old_key.strftime(TIME_FORMAT)
        self.log.debug('put_device_metrics device %s prune %s' %
                       (devid, prune))
        erase = []
        with rados.ReadOpCtx() as op:
            iter, ret = ioctx.get_omap_keys(op, "", 500) # fixme
            assert ret == 0
            ioctx.operate_read_op(op, devid)
            for key, _ in list(iter):
                if key >= prune:
                    break
                erase.append(key)
        key = datetime.now().strftime(TIME_FORMAT)
        self.log.debug('put_device_metrics device %s key %s = %s, erase %s' %
                       (devid, key, data, erase))
        with rados.WriteOpCtx() as op:
            ioctx.set_omap(op, (key,), (str(json.dumps(data)),))
            if len(erase):
                ioctx.remove_omap_keys(op, tuple(erase))
            ioctx.operate_write_op(op, devid)

    def show_device_metrics(self, devid, sample):
        # verify device exists
        r = self.get("device " + devid)
        if not r or 'device' not in r.keys():
            return (-errno.ENOENT, '', 'device ' + devid + ' not found')
        # fetch metrics
        ioctx = self.open_connection()
        res = {}
        with rados.ReadOpCtx() as op:
            iter, ret = ioctx.get_omap_vals(op, "", sample or '', 500) # fixme
            assert ret == 0
            try:
                ioctx.operate_read_op(op, devid)
                for key, value in list(iter):
                    if sample and key != sample:
                        break
                    try:
                        v = json.loads(value)
                    except:
                        self.log.debug('unable to parse value for %s: "%s"' %
                                       (key, value))
                        pass
                    res[key] = v
            except:
                pass
        return (0, json.dumps(res, indent=4), '')

    def life_expectancy_response(self):
        mark_out_threshold_td = timedelta(seconds=int(self.mark_out_threshold))
        warn_threshold_td = timedelta(seconds=int(self.warn_threshold))
        health_warnings = []
        devs = self.get("devices")
        for dev in devs['devices']:
            if 'life_expectancy_min' not in dev:
                continue
            # life_expectancy_(min/max) is in the format of:
            # '%Y-%m-%d %H:%M:%S.%f', e.g.:
            # '2019-01-20 21:12:12.000000'
            life_expectancy_min = datetime.strptime(dev['life_expectancy_min'], '%Y-%m-%d %H:%M:%S.%f')
            now = datetime.now()
            if life_expectancy_min - now <= mark_out_threshold_td:
                if self.self_heal:
                    # dev['daemons'] == ["osd.0","osd.1","osd.2"]
                    if dev['daemons']:
                        osd_ids = map(lambda x: x[4:], dev['daemons'])
                        osds_in = []
                        osds_out = []
                        for _id in osd_ids:
                            if self.is_osd_in(_id):
                                osds_in.append(_id)
                            else:
                                osds_out.append(_id)
                        if osds_in:
                            self.mark_out(osds_in)
                        # OSD might be marked 'out' (which means it has no
                        # data), however PGs are still attached to it.
                        for _id in osds_out:
                            num_pgs = self.get_osd_num_pgs(_id)
                            if num_pgs > 0:
                                health_warnings.append('osd.%s is marked out, '
                                                       'but still has %s PG(s)'
                                                       ' attached' %
                                                       (_id, num_pgs))
                        # TODO: set_primary_affinity
                self.log.warn(self.create_warning_message(dev))
            elif life_expectancy_min - now <= warn_threshold_td:
                health_warnings.append(self.create_warning_message(dev))
        if health_warnings:
            self.set_health_checks({
                'MGR_DEVICE_HEALTH': {
                    'severity': 'warning',
                    'summary': 'Imminent failure anticipated for device(s)',
                    'detail': health_warnings
                }
            })
        else:
            self.set_health_checks({}) # clearing health checks
        return (0,"","")

    def is_osd_in(self, osd_id):
        osdmap = self.get("osd_map")
        assert osdmap is not None
        for osd in osdmap['osds']:
            if str(osd_id) == str(osd['osd']):
                return osd['in']
        # return False

    def get_osd_num_pgs(self, osd_id):
        stats = self.get('osd_stats')
        assert stats is not None
        for stat in stats['osd_stats']:
            if str(osd_id) == str(stat['osd']):
                return stat['num_pgs']
        return -1

    def create_warning_message(self, dev):
        # device can appear in more than one location in case of SCSI multipath
        device_locations = map(lambda x: x['host'] + ':' + x['dev'], dev['location'])
        return ('%s at %s;'
               ' Affected OSDs: %s;'
               ' Life expectancy: between %s and %s'
               % (dev['devid'],
                device_locations,
                dev.get('daemons', 'none'),
                dev['life_expectancy_min'],
                dev.get('life_expectancy_max', 'unknown')))
                # TODO: by default, dev['life_expectancy_max'] == '0.000000',
                # so dev.get('life_expectancy_max', 'unknown')
                # above should be altered.

    def mark_out(self, osd_ids):
        self.log.info('Marking out OSDs: %s' % osd_ids)
        result = CommandResult('')
        self.send_command(result, 'mon', '', json.dumps({
            'prefix': 'osd out',
            'format': 'json',
            'ids': osd_ids,
        }), '')
        r, outb, outs = result.wait()
        if r != 0:
            self.log.warn('Could not mark OSD %s out. r: [%s], outb: [%s], outs: [%s]' % (osd_ids, r, outb, outs))

    def extract_smart_features(self, raw):
        # FIXME: extract and normalize raw smartctl --json output and
        # generate a dict of the fields we care about.
        return raw
