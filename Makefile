# Makefile for `dependencies`.
#
# Defaults aim for a small, statically-linked, deterministic build of
# a single C source file.  All defaults can be overridden on the
# command line, e.g.:
#
#     make CC=clang CFLAGS="-O2 -g"
#
# To verify a published binary against this source, run `make clean &&
# make` and compare SHA-256 sums.

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?= -static

# Reproducibility helpers: strip absolute paths from any debug
# records, drop the linker build-id, strip symbols, and pin
# SOURCE_DATE_EPOCH so any embedded timestamps are deterministic.
REPRO_CFLAGS  = -ffile-prefix-map=$(CURDIR)=.
REPRO_LDFLAGS = -Wl,--build-id=none -s
EPOCH        ?= 0

TARGET = dependencies
SRC    = dependencies.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	SOURCE_DATE_EPOCH=$(EPOCH) \
	$(CC) $(CFLAGS) $(REPRO_CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(REPRO_LDFLAGS)

clean:
	rm -f $(TARGET)
