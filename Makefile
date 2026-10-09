# Host tests, on x86 or ARM64 cores: no Amiga, no printer, no network
# (test-discovery-sendfail injects a refused mDNS send).
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
CPPFLAGS += -Iinclude
CORE = src/core/job.c src/core/ipp.c src/core/pdf_demo.c

.PHONY: all test clean test-core test-discovery test-submit test-viewer test-discovery-sendfail
all: test
test: test-core test-discovery test-submit test-viewer test-discovery-sendfail

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

build/test_submit: tests/test_submit.c src/net/ipp_submit.c src/core/ipp.c src/core/job.c src/discovery/protocol.c include/oap.h include/oap_net.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_submit.c src/net/ipp_submit.c src/core/ipp.c src/core/job.c src/discovery/protocol.c
test-submit: build/test_submit
	./build/test_submit

build/test_viewer_core: tests/test_viewer_core.c src/viewer/oav_core.c include/oav_core.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -o $@ tests/test_viewer_core.c src/viewer/oav_core.c
test-viewer: build/test_viewer_core
	./build/test_viewer_core

test-discovery-sendfail:
	sh tests/test_discovery_sendfail.sh

clean:
	rm -f build/test_core build/test_discovery build/test_submit build/test_viewer_core build/oap-firstlight.pdf
