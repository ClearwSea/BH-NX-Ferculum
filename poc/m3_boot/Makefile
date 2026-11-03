# Makefile -- the M2 loader probe, on the android32 runtime. MIT.

TARGET     := bh3_m3_boot

# Gamecard entry: Power Pro Kun Pocket R. The NPDM must match the launching
# title. The per-title config uses override_key=!L for normal click-to-play.
# Mario remains available explicitly as PROGRAM_ID=0x0100000000010000.
PROGRAM_ID ?= 0x01000C4015030000
M3_STANDALONE ?= 0
M3_CARD_ENTRY ?= 0
PORT_CFLAGS += -DM3_CARD_ENTRY=$(M3_CARD_ENTRY)

M3_TABLE_WORKER_PROBE ?= 0
PORT_CFLAGS += -DM3_TABLE_WORKER_PROBE=$(M3_TABLE_WORKER_PROBE)
PORT_CFLAGS += -DM3_STANDALONE=$(M3_STANDALONE)
PORT_NPDM_PROGRAM_ID := $(PROGRAM_ID)
PORT_NPDM_VERSION    := 0.0.1
PORT_NPDM_NAME       := BH3 M3 Boot
PORT_NPDM_ADDRSPACE ?= 2
M3_HEAP_PROBE ?= 0
PORT_CFLAGS += -DM3_HEAP_PROBE=$(M3_HEAP_PROBE)

# Real renderer: mesa32's libEGL/libGLESv2 (portlibs32).
DCR_GL_MESA := 1
M3_SOFTWARE_ETC ?= 0
M3_FRAME_LIMIT ?= 0
M3_AUTO_CAPTURE ?= 0
M3_TOUCH_TEST ?= 0
M3_VOICE_SEEK_PROBE ?= 0
M3_AVPRO_BRIDGE ?= 0
M3_CG_DECODE ?= 0
M3_CG_SELFTEST ?= 0
M3_EMBEDDED_SERVER ?= 0
SERVER_PYTHON ?= /py/target-build
SERVER_NUMPY ?= /py/numpy-static
SERVER_ZLIB ?= /py/zlib-prefix
PORT_CFLAGS += -DM3_EMBEDDED_SERVER=$(M3_EMBEDDED_SERVER)
ifeq ($(M3_EMBEDDED_SERVER),1)
PORT_CFLAGS += -I$(SERVER_PYTHON) -I$(SERVER_PYTHON)/../Python-3.11.18/Include -I$(SERVER_NUMPY)
PORT_LIBS += $(SERVER_PYTHON)/libpython3.11.a @$(SERVER_NUMPY)/objects.rsp $(SERVER_PYTHON)/libpython3.11.a -L$(SERVER_ZLIB)/lib -lz -lstdc++
endif
CG_FFMPEG ?= /ffmpeg
M3_INPUT_SAMPLER ?= 1
PORT_CFLAGS += -DM3_TOUCH_TEST=$(M3_TOUCH_TEST)
PORT_CFLAGS += -DM3_VOICE_SEEK_PROBE=$(M3_VOICE_SEEK_PROBE)
PORT_CFLAGS += -DM3_AVPRO_BRIDGE=$(M3_AVPRO_BRIDGE)
PORT_CFLAGS += -DM3_CG_DECODE=$(M3_CG_DECODE) -DM3_CG_SELFTEST=$(M3_CG_SELFTEST)
ifeq ($(M3_CG_DECODE),1)
PORT_CFLAGS += -I$(CG_FFMPEG)/include
PORT_LDFLAGS += -Wl,--wrap=rt_audout_submit
PORT_LIBS += -L$(CG_FFMPEG)/lib -lavformat -lavcodec -lavutil
endif
PORT_CFLAGS += -DM3_INPUT_SAMPLER=$(M3_INPUT_SAMPLER)
PORT_CFLAGS += -DM3_SOFTWARE_ETC=$(M3_SOFTWARE_ETC) -DM3_FRAME_LIMIT=$(M3_FRAME_LIMIT) -DM3_AUTO_CAPTURE=$(M3_AUTO_CAPTURE)
PORT_STAMP += -etc$(M3_SOFTWARE_ETC)-frames$(M3_FRAME_LIMIT)-capture$(M3_AUTO_CAPTURE)-touchtest$(M3_TOUCH_TEST)-seekprobe$(M3_VOICE_SEEK_PROBE)-avpro$(M3_AVPRO_BRIDGE)-sampler$(M3_INPUT_SAMPLER)-addr$(PORT_NPDM_ADDRSPACE)-heapprobe$(M3_HEAP_PROBE)-cgdecode$(M3_CG_DECODE)-cgtest$(M3_CG_SELFTEST)-embedded$(M3_EMBEDDED_SERVER)-standalone$(M3_STANDALONE)-tableworker$(M3_TABLE_WORKER_PROBE)-cardentry$(M3_CARD_ENTRY)

RUNTIME ?= ../../third_party/android32

# Observe original file reads without replacing their behavior.
PORT_LDFLAGS += -Wl,--wrap=b_fopen,--wrap=b_fread,--wrap=b_fclose,--wrap=b_readlink
PORT_LDFLAGS += -Wl,--wrap=b_stat,--wrap=b_fseek,--wrap=b_fseeko
PORT_LDFLAGS += -Wl,--wrap=b_open,--wrap=b_read,--wrap=b_pread,--wrap=b_pread_all,--wrap=b_mmap,--wrap=b_fdopen,--wrap=fsFileRead
PORT_LDFLAGS += -Wl,--wrap=svcMapMemory,--wrap=nvMapClose
PORT_LDFLAGS += -Wl,--wrap=nvAddressSpaceMap,--wrap=nvAddressSpaceUnmap,--wrap=nvIoctl
PORT_LDFLAGS += -Wl,--wrap=b_connect,--wrap=b_send,--wrap=b_recv,--wrap=b_getsockopt,--wrap=b_setsockopt,--wrap=b_shutdown
PORT_LDFLAGS += -Wl,--wrap=b_inflate

include $(RUNTIME)/runtime.mk

$(BUILD)/cg_decoder.o: CFLAGS += -fno-short-enums
$(BUILD)/local_server.o: CFLAGS += -fno-short-enums
$(BUILD)/serializer_cache_guard.o: CFLAGS += -fexceptions
