# ACNet mDNS integration fix

The installed ACNet HostSocket rejected every multicast destination in `_dest()`.
That included the one-shot DNS-SD printer query to UDP 224.0.0.251:5353. The
unpatched worker ignored `sendto()` failures and reported zero endpoints.

`acnet-mdns.patch` is the narrowly scoped installed-runtime patch. Its canonical
source equivalent is in DalsinAI/amigachrome branch `fix/acnet-mdns-discovery`,
commit e9e046f, at machines/motherboards/a1200-x86/native/hostsocket.py.
The installed copy is host/native/hostsocket.py. Restart an affected instance's
runtime after applying; restarting only the printer browser will not reload Python.

Only UDP to 224.0.0.251 port 5353 is allowed. Other multicast destinations, multicast
TCP, raw sockets, access to the host and the disabled Network switch retain their
existing restrictions. Queries use a one-shot ephemeral UDP socket and replies
are unicast, so a multicast-listening service is not required.

Tests:

    python3 integration/cradle/test_hostsocket_mdns.py /path/to/hostsocket.py
    python3 integration/cradle/test_hostsocket_mdns.py /path/to/hostsocket.py --live
    sh tests/test_discovery_sendfail.sh

The optional live test sends one mDNS query only; it does not send a print job.

On 3 October 2026, the original policy failure was reproduced as Amiga errno 51.
The patched HostSocket received responses from all three LAN printers, and all
nine policy/live tests passed. The canonical host suite passed 22 tests. The
OpenAmigaPrint codec suite passed 942 checks, and the send-failure injection test
proved that a blocked query produces a completed error, not an empty success.

Native acceptance is NOT yet complete. After deployment, one Instance-23 restart
showed a C:ACClip software failure; the native discovery retest then stalled while
opening bsdsocket.library. These are observations, not a claim that ACClip is the
cause of the network-library stall. No physical printing was attempted.
