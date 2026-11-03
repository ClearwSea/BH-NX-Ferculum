# Makefile -- the M1 probe, on the android32 runtime. MIT.

TARGET               := bh3_m1_probe

# The ExeFS-override harness: the NPDM's program id must match the title that
# launches it. Default: Super Mario Odyssey (0100000000010000); the per-title
# config.ini sets override_key=R, so the game runs normally without R.
PROGRAM_ID           ?= 0x0100000000010000
PORT_NPDM_PROGRAM_ID := $(PROGRAM_ID)
PORT_NPDM_VERSION    := 0.0.1
PORT_NPDM_NAME       := BH3 M1 Probe

# Local checkout: ../../third_party/android32. The remote build mounts the
# runtime at /runtime and passes RUNTIME=/runtime.
RUNTIME ?= ../../third_party/android32

include $(RUNTIME)/runtime.mk
