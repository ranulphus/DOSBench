# Build configuration; override in config.local.mk (not committed).
# DOSBench builds against a checkout of RetroGPU beside it: the Glide runtime
# (glide/), DOS-GL (gl/, libGL.a), the HAL, the test shim and the Loop A
# harness, on its main as it is (no pins).
RETROGPU     ?= $(HOME)/RetroGPU
FIFTHWHEEL   ?= $(HOME)/FifthWheel
WATCOM       ?= $(HOME)/.local/opt/watcom-20260901
DJGPP_PREFIX ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC      ?= gcc
PYTHON       ?= python3
