# DOSBench: a Glide 2.x and OpenGL 1.1 benchmark for DOS (docs/).
#
#   make                  BENCHG.EXE (Glide), BENCHGL.EXE (OpenGL), DBMENU.EXE
#   make tests-host       host unit tests
#   make data             fetch and convert the scenes (tools/assets.py)
#   make loopa CARD=g450  run both programs in 86Box (tools/run.py)
#   make check-deps       sibling checkouts at or after the pinned commits
include config.mk
-include config.local.mk
include deps.mk

BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
Q ?= @
MGAHAL := $(DOSGL)/third_party/mgahal

CORE_SRCS := $(wildcard src/core/*.c)
.PHONY: all deps check-deps tests-host data loopa clean help
all: build/dos/BENCHG.EXE build/dos/BENCHGL.EXE build/dos/DBMENU.EXE

# ---- Sibling checkouts --------------------------------------------------
check-deps:
	@for d in "$(MGA_GLIDE):$(MGA_GLIDE_PIN)" "$(DOSGL):$(DOSGL_PIN)"; do \
	  dir=$${d%%:*}; pin=$${d##*:}; \
	  git -C "$$dir" merge-base --is-ancestor "$$pin" HEAD 2>/dev/null \
	    || { echo "$$dir is not at or after $$pin (deps.mk)"; exit 1; }; \
	done
# The pieces DOSBench uses from each, built in place.
deps: check-deps
	$(Q)$(MAKE) -s -C $(MGA_GLIDE) build/gen/stamp build/ow/mgahal_exe.lib runtime dostools
	$(Q)$(MAKE) -s -C $(DOSGL) lib

# ---- BENCHG.EXE: Open Watcom, DOS/4GW, Glide through MGA-Glide's loader --
OWBIN := $(WATCOM)/binl64
OWENV := env WATCOM=$(WATCOM) INCLUDE=$(WATCOM)/h PATH=$(OWBIN):$(PATH)
WCC   := $(OWENV) $(OWBIN)/wcc386
WLINK := $(OWENV) $(OWBIN)/wlink
OW_CFLAGS := -bt=dos -mf -3s -fp5 -fpi87 -zri -ei -j -zastd=c99 -zq -we -wx -oxt \
             -i=src/core -i=src/backend -i=$(MGA_GLIDE)/include -i=$(MGA_GLIDE)/hal/include \
             -i=$(MGA_GLIDE)/build/gen -i=$(MGA_GLIDE)/tests/shim -i=build/gen -dMGA_OW=1 \
             -dDB_PROG="\"BENCHG\"" -dDB_BUILD_ID="\"$(BUILD_ID)\"" -dHX_BUILD_ID="\"$(BUILD_ID)\""
SHIM_SRCS := $(MGA_GLIDE)/tests/shim/hx.c $(MGA_GLIDE)/tests/shim/leload.c $(MGA_GLIDE)/tests/shim/glbind.c \
             $(MGA_GLIDE)/build/gen/glapi_names.c
G_OBJS := $(patsubst %.c,build/ow/%.obj,$(CORE_SRCS) src/backend/rb_glide.c src/backend/gtex.c) \
          $(patsubst $(MGA_GLIDE)/%.c,build/ow/mga/%.obj,$(SHIM_SRCS))

build/ow/%.obj: %.c | deps
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
build/ow/mga/%.obj: $(MGA_GLIDE)/%.c | deps
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $(notdir $<)"
	$(Q)$(WCC) $(OW_CFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
-include $(shell find build/ow -name '*.d' 2>/dev/null)

build/dos/BENCHG.EXE: $(G_OBJS) $(MGA_GLIDE)/build/ow/mgahal_exe.lib
	@mkdir -p $(dir $@)
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) system dos4g option quiet option stack=128k name $@ $(addprefix file ,$(G_OBJS)) \
	  library $(MGA_GLIDE)/build/ow/mgahal_exe.lib option map=$(@:.EXE=.map)

# ---- BENCHGL.EXE and DBMENU.EXE: DJGPP, CWSDPMI, DOS-GL's libGL.a ---------
DJENV := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib
DJCC  := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
DJ_CFLAGS := -std=gnu99 -O2 -march=i586 -Wall -Wextra -Werror -Isrc/core -Isrc/backend -Ibuild/gen \
             -I$(DOSGL)/include -I$(MGAHAL)/hal/include -I$(MGAHAL)/tests/shim -DMGA_DJGPP=1 \
             -DDB_BUILD_ID='"$(BUILD_ID)"' -DHX_BUILD_ID='"$(BUILD_ID)"'
L_OBJS := $(patsubst %.c,build/dj/%.o,$(CORE_SRCS) src/backend/rb_gl.c) build/dj/shim/hx.o

build/dj/%.o: %.c | deps
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) $(DJ_CFLAGS) -DDB_PROG='"BENCHGL"' -MMD -c -o $@ $<
build/dj/shim/hx.o: $(MGAHAL)/tests/shim/hx.c | deps
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    hx.c"
	$(Q)$(DJCC) $(DJ_CFLAGS) -MMD -c -o $@ $<
-include $(shell find build/dj -name '*.d' 2>/dev/null)

build/dos/BENCHGL.EXE: $(L_OBJS) $(DOSGL)/build/lib/libGL.a
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) -o $@ $(L_OBJS) $(DOSGL)/build/lib/libGL.a -lm
	@if [ -e "$(@:.EXE=.exe)" ] && ! [ "$(@:.EXE=.exe)" -ef "$@" ]; then rm -f "$(@:.EXE=.exe)"; fi

# The test registry (src/core/tests.json): one generated header for both
# programs and the menu, rewritten only when it changes.
build/gen/registry.h: tools/registry.py src/core/tests.json tools/games.json
	$(Q)$(PYTHON) tools/registry.py gen $@
$(G_OBJS) $(L_OBJS): | build/gen/registry.h

# The menu: no graphics, no shim; its test catalogue is the registry.
build/dos/DBMENU.EXE: src/menu/menu.c build/gen/registry.h
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) -std=gnu99 -O2 -march=i386 -Wall -Wextra -Werror -Ibuild/gen -o $@ $<
	@if [ -e "$(@:.EXE=.exe)" ] && ! [ "$(@:.EXE=.exe)" -ef "$@" ]; then rm -f "$(@:.EXE=.exe)"; fi

# ---- Host unit tests ------------------------------------------------------
HOST_CFLAGS := -std=gnu99 -O1 -g -Wall -Wextra -Werror -Isrc/core -Isrc/backend -Ibuild/gen -I$(MGA_GLIDE)/include
UNIT_TESTS := stats vmath clip tex timer scene select
UNIT_stats := src/core/stats.c
UNIT_vmath := src/core/vmath.c
UNIT_clip  := src/core/clip.c
UNIT_tex   := src/core/texutil.c src/backend/gtex.c
UNIT_timer := src/core/timer.c src/core/stats.c
UNIT_scene := src/core/scene.c src/core/vmath.c
UNIT_select := src/core/select.c src/core/registry.c
build/host/test_select: build/gen/registry.h
.SECONDEXPANSION:
build/host/test_%: tests/unit/test_%.c tests/unit/unit.c tests/unit/unit.h $$(UNIT_$$*) $(wildcard src/core/*.h src/backend/*.h)
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -o $@ $< tests/unit/unit.c $(UNIT_$*) -lm
build/host/selftest.dbs: tools/dbs.py
	@mkdir -p $(dir $@)
	$(Q)$(PYTHON) tools/dbs.py --selftest $@
tests-host: $(UNIT_TESTS:%=build/host/test_%) build/host/selftest.dbs
	@set -e; for t in $(UNIT_TESTS:%=build/host/test_%); do echo "== $$t"; $$t; done
	$(Q)$(PYTHON) tools/registry.py check
	$(Q)$(PYTHON) tools/registry.py selftest

# ---- Content and runs -----------------------------------------------------
data:
	$(PYTHON) tools/assets.py fetch convert
CARD ?= g450
loopa: all
	$(PYTHON) tools/run.py loopa --card $(CARD)

clean:
	rm -rf build out

help:
	@sed -n '3,7p' Makefile
