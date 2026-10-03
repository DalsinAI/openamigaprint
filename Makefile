CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -Iinclude
CORE = src/core/job.c src/core/ipp.c src/core/pdf_demo.c

.PHONY: all test clean
all: test
build/test_core: tests/test_core.c $(CORE) include/oap.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -o $@ tests/test_core.c $(CORE)
test: build/test_core
	./build/test_core
clean:
	rm -f build/test_core build/oap-firstlight.pdf

.PHONY: test-discovery
build/test_discovery: tests/test_discovery.c src/discovery/protocol.c src/discovery/http.c include/oap_discovery.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-misleading-indentation -o $@ tests/test_discovery.c src/discovery/protocol.c src/discovery/http.c
test-discovery: build/test_discovery
	./build/test_discovery

.PHONY: test-submit
build/test_submit: tests/test_submit.c src/amiga/ipp_transport.c src/core/ipp.c src/core/job.c src/discovery/protocol.c include/oap.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-misleading-indentation -o $@ tests/test_submit.c src/amiga/ipp_transport.c src/core/ipp.c src/core/job.c src/discovery/protocol.c
test-submit: build/test_submit
	./build/test_submit
