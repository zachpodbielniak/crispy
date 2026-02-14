# Makefile - Crispy (Crispy Really Is Super Powerful Yo)
# A GLib/GObject/GIO-native C scripting tool
#
# Usage:
#   make              - Build all (lib + crispy executable)
#   make lib          - Build static and shared libraries
#   make crispy       - Build the crispy executable
#   make gir          - Generate GIR/typelib for introspection
#   make test         - Run the test suite
#   make install      - Install to PREFIX
#   make clean        - Clean build artifacts
#   make DEBUG=1      - Build with debug symbols
#   make ASAN=1       - Build with AddressSanitizer

.DEFAULT_GOAL := all
.PHONY: all lib crispy gir test check-deps

# Include configuration
include config.mk

# Check dependencies before anything else (skip for targets that don't need them)
SKIP_DEP_CHECK_TARGETS := install-deps help check-deps show-config clean clean-all
ifeq ($(filter $(SKIP_DEP_CHECK_TARGETS),$(MAKECMDGOALS)),)
$(foreach dep,$(DEPS_REQUIRED),$(call check_dep,$(dep)))
endif

# Source files - Library
LIB_SRCS := \
	src/interfaces/crispy-compiler.c \
	src/interfaces/crispy-cache-provider.c \
	src/core/crispy-gcc-compiler.c \
	src/core/crispy-file-cache.c \
	src/core/crispy-script.c

# Header files (for GIR scanner and installation)
LIB_HDRS := \
	src/crispy.h \
	src/crispy-types.h \
	src/crispy-version.h \
	src/interfaces/crispy-compiler.h \
	src/interfaces/crispy-cache-provider.h \
	src/core/crispy-gcc-compiler.h \
	src/core/crispy-file-cache.h \
	src/core/crispy-script.h

# Test sources
TEST_SRCS := $(wildcard tests/test-*.c)

# Object files
LIB_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(LIB_SRCS))
MAIN_OBJ := $(OBJDIR)/main.o
TEST_OBJS := $(patsubst tests/%.c,$(OBJDIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst tests/%.c,$(OUTDIR)/%,$(TEST_SRCS))

# Include build rules
include rules.mk

# Default target
all: lib crispy
ifeq ($(BUILD_GIR),1)
all: gir
endif

# Build the library
lib: src/crispy-version.h $(OUTDIR)/$(LIB_STATIC) $(OUTDIR)/$(LIB_SHARED_FULL) $(OUTDIR)/crispy.pc

# Build the executable
crispy: lib $(OUTDIR)/crispy

# Build GIR/typelib
gir: $(OUTDIR)/$(GIR_FILE) $(OUTDIR)/$(TYPELIB_FILE)

# Build and run tests
test: lib $(TEST_BINS)
	@echo "Running tests..."
	@failed=0; \
	for test in $(TEST_BINS); do \
		echo "  Running $$(basename $$test)..."; \
		if LD_LIBRARY_PATH=$(OUTDIR) $$test; then \
			echo "    PASS"; \
		else \
			echo "    FAIL"; \
			failed=$$((failed + 1)); \
		fi \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test(s) failed"; \
		exit 1; \
	else \
		echo "All tests passed"; \
	fi

# Build individual test binaries
$(OUTDIR)/test-%: $(OBJDIR)/tests/test-%.o $(OUTDIR)/$(LIB_SHARED_FULL)
	$(CC) -o $@ $< $(TEST_LDFLAGS)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@for dep in $(DEPS_REQUIRED); do \
		if $(PKG_CONFIG) --exists $$dep; then \
			echo "  $$dep: OK ($$($(PKG_CONFIG) --modversion $$dep 2>/dev/null))"; \
		else \
			echo "  $$dep: MISSING"; \
		fi \
	done

# Help target
.PHONY: help
help:
	@echo "Crispy - Crispy Really Is Super Powerful Yo"
	@echo ""
	@echo "Build targets:"
	@echo "  all        - Build library and executable (default)"
	@echo "  lib        - Build static and shared libraries"
	@echo "  crispy     - Build the crispy executable"
	@echo "  gir        - Generate GObject Introspection data"
	@echo "  test       - Build and run the test suite"
	@echo "  install    - Install to PREFIX ($(PREFIX))"
	@echo "  uninstall  - Remove installed files"
	@echo "  clean      - Remove build artifacts"
	@echo "  clean-all  - Remove all build directories"
	@echo ""
	@echo "Build options (set on command line):"
	@echo "  DEBUG=1       - Enable debug build"
	@echo "  ASAN=1        - Enable AddressSanitizer"
	@echo "  PREFIX=path   - Set installation prefix"
	@echo "  BUILD_GIR=1   - Enable GIR generation"
	@echo "  BUILD_TESTS=0 - Disable test building"
	@echo ""
	@echo "Utility targets:"
	@echo "  install-deps - Install build dependencies (Fedora/dnf)"
	@echo "  check-deps   - Check for required dependencies"
	@echo "  show-config  - Show current build configuration"
	@echo "  help         - Show this help message"

# Dependency tracking (optional, for incremental builds)
-include $(LIB_OBJS:.o=.d)
-include $(MAIN_OBJ:.o=.d)
