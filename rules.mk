# rules.mk - Crispy Build Rules
# Pattern rules and common build recipes

# All source objects depend on the generated version header
$(LIB_OBJS) $(MAIN_OBJ): src/crispy-version.h

# main.o depends on generated headers
$(MAIN_OBJ): $(OUTDIR)/crispy-default-config.h
$(MAIN_OBJ): $(OUTDIR)/crispy-logo.h

# Object file compilation
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/core/%.o: src/core/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/interfaces/%.o: src/interfaces/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Test compilation
$(OBJDIR)/tests/%.o: tests/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

# Static library creation
$(OUTDIR)/$(LIB_STATIC): $(LIB_OBJS)
	@$(MKDIR_P) $(dir $@)
	$(AR) rcs $@ $^

# Shared library creation
$(OUTDIR)/$(LIB_SHARED_FULL): $(LIB_OBJS)
	@$(MKDIR_P) $(dir $@)
	$(CC) $(LDFLAGS_SHARED) -o $@ $^ $(LDFLAGS)
	cd $(OUTDIR) && ln -sf $(LIB_SHARED_FULL) $(LIB_SHARED_MAJOR)
	cd $(OUTDIR) && ln -sf $(LIB_SHARED_MAJOR) $(LIB_SHARED)

# Executable linking
$(OUTDIR)/crispy: $(OBJDIR)/main.o $(OUTDIR)/$(LIB_SHARED_FULL)
	$(CC) -o $@ $(OBJDIR)/main.o -L$(OUTDIR) -lcrispy $(LDFLAGS) -Wl,-rpath,'$$ORIGIN'

# GIR generation
$(OUTDIR)/$(GIR_FILE): $(LIB_SRCS) $(LIB_HDRS) | $(OUTDIR)/$(LIB_SHARED_FULL)
	$(GIR_SCANNER) \
		--namespace=$(GIR_NAMESPACE) \
		--nsversion=$(GIR_VERSION) \
		--library=crispy \
		--library-path=$(OUTDIR) \
		--include=GLib-2.0 \
		--include=GObject-2.0 \
		--include=Gio-2.0 \
		--pkg=glib-2.0 \
		--pkg=gobject-2.0 \
		--pkg=gio-2.0 \
		--output=$@ \
		--warn-all \
		-Isrc \
		$(LIB_HDRS) $(LIB_SRCS)

# Typelib compilation
$(OUTDIR)/$(TYPELIB_FILE): $(OUTDIR)/$(GIR_FILE)
	$(GIR_COMPILER) --output=$@ $<

# Directory creation
$(OBJDIR):
	@$(MKDIR_P) $(OBJDIR)
	@$(MKDIR_P) $(OBJDIR)/core
	@$(MKDIR_P) $(OBJDIR)/interfaces
	@$(MKDIR_P) $(OBJDIR)/tests

$(OUTDIR):
	@$(MKDIR_P) $(OUTDIR)

# pkg-config file generation
$(OUTDIR)/crispy.pc: crispy.pc.in | $(OUTDIR)
	sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@LIBDIR@|$(LIBDIR)|g' \
		-e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
		-e 's|@VERSION@|$(VERSION)|g' \
		$< > $@

# Version header generation
src/crispy-version.h: src/crispy-version.h.in
	sed \
		-e 's|@CRISPY_VERSION_MAJOR@|$(VERSION_MAJOR)|g' \
		-e 's|@CRISPY_VERSION_MINOR@|$(VERSION_MINOR)|g' \
		-e 's|@CRISPY_VERSION_MICRO@|$(VERSION_MICRO)|g' \
		-e 's|@CRISPY_VERSION@|$(VERSION)|g' \
		$< > $@

# Default config header generation (embeds data/default-config.c as a C string)
$(OUTDIR)/crispy-default-config.h: data/default-config.c | $(OUTDIR)
	@$(MKDIR_P) $(dir $@)
	@echo "  GEN     $@"
	@echo "static const gchar *default_c_config =" > $@
	@sed 's/\\/\\\\/g; s/"/\\"/g; s/	/\\t/g; s/^/"/; s/$$/\\n"/' $< >> $@
	@echo ";" >> $@

# Logo ASCII art header generation (embeds data/logo.txt as a C string)
$(OUTDIR)/crispy-logo.h: data/logo.txt | $(OUTDIR)
	@$(MKDIR_P) $(dir $@)
	@echo "  GEN     $@"
	@echo "static const gchar *crispy_logo_text =" > $@
	@sed 's/\\/\\\\/g; s/"/\\"/g; s/	/\\t/g; s/^/"/; s/$$/\\n"/' $< >> $@
	@echo ";" >> $@

# Header dependency generation
$(OBJDIR)/%.d: src/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT '$(@:.d=.o)' $< > $@

# Clean rules
.PHONY: clean clean-all
clean:
	rm -rf $(BUILDDIR)/$(BUILD_TYPE)
	rm -f src/crispy-version.h

clean-all:
	rm -rf $(BUILDDIR)
	rm -f src/crispy-version.h
	rm -f $(OUTDIR)/crispy-default-config.h
	rm -f $(OUTDIR)/crispy-logo.h

# Installation rules
.PHONY: install install-lib install-bin install-headers install-pc install-gir install-data

install: install-lib install-bin install-headers install-pc install-data
ifeq ($(BUILD_GIR),1)
install: install-gir
endif

install-bin: $(MAIN_OBJ) $(OUTDIR)/$(LIB_SHARED_FULL)
	$(MKDIR_P) $(DESTDIR)$(BINDIR)
	$(CC) -o $(DESTDIR)$(BINDIR)/crispy $(MAIN_OBJ) \
		-L$(OUTDIR) -lcrispy $(LDFLAGS) -Wl,-rpath,$(LIBDIR)

install-lib: $(OUTDIR)/$(LIB_STATIC) $(OUTDIR)/$(LIB_SHARED_FULL)
	$(MKDIR_P) $(DESTDIR)$(LIBDIR)
	$(INSTALL_PROGRAM) $(OUTDIR)/$(LIB_SHARED_FULL) $(DESTDIR)$(LIBDIR)/
	$(INSTALL_DATA) $(OUTDIR)/$(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	cd $(DESTDIR)$(LIBDIR) && ln -sf $(LIB_SHARED_FULL) $(LIB_SHARED_MAJOR)
	cd $(DESTDIR)$(LIBDIR) && ln -sf $(LIB_SHARED_MAJOR) $(LIB_SHARED)
	-ldconfig 2>/dev/null || true

install-headers:
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/crispy
	$(INSTALL_DATA) src/crispy.h $(DESTDIR)$(INCLUDEDIR)/crispy/
	$(INSTALL_DATA) src/crispy-types.h $(DESTDIR)$(INCLUDEDIR)/crispy/
	$(INSTALL_DATA) src/crispy-version.h $(DESTDIR)$(INCLUDEDIR)/crispy/
	$(INSTALL_DATA) src/crispy-plugin.h $(DESTDIR)$(INCLUDEDIR)/crispy/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/crispy/interfaces
	$(INSTALL_DATA) src/interfaces/*.h $(DESTDIR)$(INCLUDEDIR)/crispy/interfaces/
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/crispy/core
	for hdr in src/core/*.h; do \
		case $$hdr in *-private.h) continue ;; esac; \
		$(INSTALL_DATA) $$hdr $(DESTDIR)$(INCLUDEDIR)/crispy/core/; \
	done

install-data: data/default-config.c
	$(MKDIR_P) $(DESTDIR)$(DATADIR)/crispy
	$(INSTALL_DATA) data/default-config.c $(DESTDIR)$(DATADIR)/crispy/config.c

install-pc: $(OUTDIR)/crispy.pc
	$(MKDIR_P) $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL_DATA) $(OUTDIR)/crispy.pc $(DESTDIR)$(PKGCONFIGDIR)/

install-gir: $(OUTDIR)/$(GIR_FILE) $(OUTDIR)/$(TYPELIB_FILE)
	$(MKDIR_P) $(DESTDIR)$(GIRDIR)
	$(MKDIR_P) $(DESTDIR)$(TYPELIBDIR)
	$(INSTALL_DATA) $(OUTDIR)/$(GIR_FILE) $(DESTDIR)$(GIRDIR)/
	$(INSTALL_DATA) $(OUTDIR)/$(TYPELIB_FILE) $(DESTDIR)$(TYPELIBDIR)/

# Uninstall
.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/crispy
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_FULL)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_MAJOR)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -rf $(DESTDIR)$(INCLUDEDIR)/crispy
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/crispy.pc
	rm -f $(DESTDIR)$(GIRDIR)/$(GIR_FILE)
	rm -f $(DESTDIR)$(TYPELIBDIR)/$(TYPELIB_FILE)
	rm -f $(DESTDIR)$(DATADIR)/crispy/config.c
	-rmdir $(DESTDIR)$(DATADIR)/crispy 2>/dev/null || true
