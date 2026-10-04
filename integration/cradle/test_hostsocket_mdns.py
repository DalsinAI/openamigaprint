#!/usr/bin/env python3
"""Exercise an installed/staged ACNet hostsocket module, without printing."""
import importlib.util
import os
import struct
import sys
import time
import unittest

MODULE = sys.argv.pop(1)
LIVE = '--live' in sys.argv
if LIVE:
    sys.argv.remove('--live')
spec = importlib.util.spec_from_file_location('oap_test_hostsocket', MODULE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class MDNSPolicy(unittest.TestCase):
    def setUp(self):
        self.enabled = True
        self.h = m.HostSocket(lambda: {'enabled': self.enabled}, lambda: None,
                              addresses=lambda: {'192.0.2.50'})
        self.h._refresh()

    def tearDown(self):
        self.h.close()

    def denied(self, ip, port, kind):
        with self.assertRaises(m.Err):
            self.h._dest(ip, port, kind)

    def test_local_mdns_udp_allowed(self):
        self.assertEqual(self.h._dest('224.0.0.251', 5353, m.SOCK_DGRAM),
                         ('224.0.0.251', 5353))

    def test_multicast_tcp_stays_denied(self):
        self.denied('224.0.0.251', 5353, m.SOCK_STREAM)

    def test_other_mdns_ports_stay_denied(self):
        for port in (53, 631, 5354, 1900):
            self.denied('224.0.0.251', port, m.SOCK_DGRAM)

    def test_other_multicast_stays_denied(self):
        for ip in ('224.0.0.1', '224.0.0.252', '239.255.255.250'):
            self.denied(ip, 5353, m.SOCK_DGRAM)

    def test_host_isolation_preserved(self):
        self.denied('192.0.2.50', 631, m.SOCK_STREAM)
        self.denied('192.0.2.50', 5353, m.SOCK_DGRAM)

    def test_loopback_isolation_preserved(self):
        self.denied('127.0.0.1', 631, m.SOCK_STREAM)

    def test_network_off_blocks_discovery(self):
        err, fd, _ = self.h.command(m.CMD_SOCKET,m.AF_INET,m.SOCK_DGRAM,0,0,b'')
        self.assertEqual(err, 0)
        self.enabled = False
        err, _, _ = self.h.command(m.CMD_SENDTO,fd,0,0,0,
                                   m.sockaddr('224.0.0.251',5353)+b'query')
        self.assertEqual(err,m.A['ENETDOWN'])

    def test_unicast_printers_preserved(self):
        self.assertEqual(self.h._dest('192.0.2.60',631,m.SOCK_STREAM),
                         ('192.0.2.60',631))

    @unittest.skipUnless(LIVE, 'pass --live to send one mDNS query on the LAN')
    def test_live_reply_through_hostsocket(self):
        err, fd, _ = self.h.command(m.CMD_SOCKET,m.AF_INET,m.SOCK_DGRAM,0,0,b'')
        self.assertEqual(err,0)
        err, _, _ = self.h.command(m.CMD_BIND,fd,0,0,0,m.sockaddr('0.0.0.0',0))
        self.assertEqual(err,0)
        query = struct.pack('>6H',0x4f41,0,1,0,0,0) + b'\x04_ipp\x04_tcp\x05local\x00' + struct.pack('>HH',12,1)
        err, sent, _ = self.h.command(m.CMD_SENDTO,fd,0,0,0,m.sockaddr('224.0.0.251',5353)+query)
        self.assertEqual(err,0)
        self.assertEqual(sent,len(query))
        replies = 0
        end=time.monotonic()+4
        while time.monotonic()<end:
            err, n, data=self.h.command(m.CMD_RECVFROM,fd,9000,0,0,b'')
            if err==m.A['EAGAIN']:
                time.sleep(.05)
                continue
            self.assertEqual(err,0)
            ip,port=m.parse_sockaddr(data[:16])
            if port==5353 and n>=12 and data[18]&0x80:
                replies+=1
                print('HostSocket mDNS reply:',ip,'bytes',n,flush=True)
        self.assertGreater(replies,0,'No live mDNS replies received')
        print('Live HostSocket discovery:',replies,'replies; no print job sent',flush=True)

if __name__=='__main__':
    unittest.main(verbosity=2)
