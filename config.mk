# Build configuration; override in config.local.mk (not committed).
# DOSBench builds against sibling checkouts of MGA-Glide (Glide runtime,
# shared HAL, test shim, Loop A harness) and DOS-GL (libGL.a); deps.mk pins
# the commits they must be at or descended from.
MGA_GLIDE    ?= $(HOME)/MGA-Glide
DOSGL        ?= $(HOME)/DOSGL
FIFTHWHEEL   ?= $(HOME)/FifthWheel
WATCOM       ?= $(HOME)/.local/opt/watcom-20260901
DJGPP_PREFIX ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC      ?= gcc
PYTHON       ?= python3
