# Host tests, on x86 or ARM64 cores: no Amiga, no printer, no network
# (test-discovery-sendfail injects a refused mDNS send).
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
CPPFLAGS += -Iinclude
CORE = src/core/job.c src/core/ipp.c src/core/pdf_demo.c

.PHONY: all test clean test-core test-discovery test-submit test-auth test-viewer test-discovery-sendfail test-ipps
all: test
test: test-core test-discovery test-submit test-auth test-viewer test-discovery-sendfail

build/test_core: tests/test_core.c $(CORE) include/oap.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -o $@ tests/test_core.c $(CORE)
test-core: build/test_core
	./build/test_core

build/test_discovery: tests/test_discovery.c src/discovery/protocol.c src/discovery/http.c include/oap_discovery.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_discovery.c src/discovery/protocol.c src/discovery/http.c
test-discovery: build/test_discovery
	./build/test_discovery

build/test_submit: tests/test_submit.c src/net/ipp_submit.c src/core/ipp.c src/core/job.c src/discovery/protocol.c src/tls/tls.c src/tls/trust.c include/oap.h include/oap_net.h include/oap_tls.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_submit.c src/net/ipp_submit.c src/core/ipp.c src/core/job.c src/discovery/protocol.c src/tls/tls.c src/tls/trust.c
test-submit: build/test_submit
	./build/test_submit

build/test_auth: tests/test_auth.c src/net/http_auth.c src/tls/tls.c src/tls/trust.c src/discovery/protocol.c src/discovery/http.c include/oap_net.h include/oap_tls.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ tests/test_auth.c src/net/http_auth.c src/tls/tls.c src/tls/trust.c src/discovery/protocol.c src/discovery/http.c
test-auth: build/test_auth
	./build/test_auth

build/test_viewer_core: tests/test_viewer_core.c src/viewer/oav_core.c include/oav_core.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -o $@ tests/test_viewer_core.c src/viewer/oav_core.c
test-viewer: build/test_viewer_core
	./build/test_viewer_core

test-discovery-sendfail:
	sh tests/test_discovery_sendfail.sh

# IPPS end to end against printers on 127.0.0.1 (ippeveprinter and a login
# printer), both TLS backends; skips when ippeveprinter or OpenSSL is missing.
test-ipps:
	sh tests/host/test_ipps.sh

clean:
	rm -f build/test_core build/test_discovery build/test_submit build/test_auth build/test_viewer_core build/oap-firstlight.pdf
