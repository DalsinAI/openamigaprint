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
