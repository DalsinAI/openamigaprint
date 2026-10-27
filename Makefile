# Host tests, on x86 or ARM64 cores: no Amiga, no printer, no network
# (test-discovery-sendfail injects a refused mDNS send).
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
CPPFLAGS += -Iinclude
CORE = src/core/job.c src/core/ipp.c src/core/pdf_demo.c

.PHONY: all test clean test-core test-discovery test-submit test-auth test-viewer test-driver test-discovery-sendfail test-ipps test-paper test-paper-page test-rawps
all: test
test: test-core test-discovery test-submit test-auth test-viewer test-driver test-discovery-sendfail test-paper test-paper-page

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

# The printer driver (the PDF writer behind printer.device), against stub headers in tests/host/driver
build/test_driver: tests/test_driver.c src/driver/openprint_driver.c $(wildcard tests/host/driver/*/*.h)
	mkdir -p build
	$(CC) -Itests/host/driver $(CFLAGS) -o $@ tests/test_driver.c src/driver/openprint_driver.c
test-driver: build/test_driver
	./build/test_driver

test-discovery-sendfail:
	sh tests/test_discovery_sendfail.sh

# Where the Print window's paper starts (.job, OpenPrefs' Printing page, its own) and each size's IPP media name;
# writes each paper's Print-Job request start to build/paper-*.ipp (files only, nothing sent).
build/test_paper: tests/test_paper.c src/core/paper.c src/core/ipp.c src/core/job.c include/oap.h include/oap_paper.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_paper.c src/core/paper.c src/core/ipp.c src/core/job.c
test-paper: build/test_paper
	./build/test_paper

# The Printing page's own writer (openamigaprefs Printing/src/pr_prefs.c) against paper.c's reader; skips with no
# openamigaprefs checkout beside this one (OPENAMIGAPREFS=dir).
test-paper-page:
	sh tests/test_paper_page.sh

# IPPS end to end against printers on 127.0.0.1 (ippeveprinter and a login
# printer), the OpenTLS backend; skips when ippeveprinter or OpenSSL is missing.
test-ipps:
	sh tests/host/test_ipps.sh

# Raw PostScript end to end against stand-in printers on 127.0.0.1 (tests/host/ps_printer.py; ippeveprinter
# too when installed): byte for byte, its attributes, refusals and no hang. Prints nothing; skips with no python3.
test-rawps:
	sh tests/test_rawps.sh

clean:
	rm -f build/test_core build/test_discovery build/test_submit build/test_auth build/test_viewer_core build/test_driver build/oap-firstlight.pdf
	rm -f build/test_paper build/test_paper_page build/paper-*.ipp
	rm -rf build/rawps
