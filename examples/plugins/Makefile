# Makefile - Crispy example plugins
#
# Usage:
#   make                - Build all example plugins
#   make clean          - Remove built plugins
#
# These plugins demonstrate the Crispy plugin API. Build them from the
# project root with:
#   make -C examples/plugins

CC ?= gcc
PKG_CONFIG ?= pkg-config
CFLAGS := -std=gnu89 -Wall -Wextra -Wno-unused-parameter -fPIC -O2
CFLAGS += -I../../src
CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0)
LDFLAGS := $(shell $(PKG_CONFIG) --libs glib-2.0)

PLUGINS := \
	plugin-timing.so \
	plugin-source-guard.so

.PHONY: all clean

all: $(PLUGINS)

%.so: %.c
	$(CC) $(CFLAGS) -shared -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PLUGINS)
