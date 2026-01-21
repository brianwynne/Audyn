# Audyn - AES67 Audio Capture & Archival Engine
# Makefile for Ubuntu 22.04+, Debian 12+
#
# Copyright: (c) 2026 B. Wynne
# Author: B. Wynne
# License: GPLv2 or later

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g
LDFLAGS := -lpthread -lm

# Package config for dependencies
PKG_CONFIG := pkg-config
PW_CFLAGS  := $(shell $(PKG_CONFIG) --cflags libpipewire-0.3)
PW_LIBS    := $(shell $(PKG_CONFIG) --libs libpipewire-0.3)
OPUS_CFLAGS := $(shell $(PKG_CONFIG) --cflags opus ogg)
OPUS_LIBS   := $(shell $(PKG_CONFIG) --libs opus ogg)

# Include paths
INCLUDES := -Icore -Iinput -Isink

# Source files
SRCS := audyn.c \
        core/log.c \
        core/frame_pool.c \
        core/audio_queue.c \
        core/ptp_clock.c \
        core/jitter_buffer.c \
        core/archive_policy.c \
        core/level_meter.c \
        core/vox.c \
        core/sdp_parser.c \
        core/sap_discovery.c \
        sink/wav_sink.c \
        sink/opus_sink.c \
        input/pipewire_input.c \
        input/aes_input.c

# Object files
OBJS := $(SRCS:.c=.o)

# Target
TARGET := audyn

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(PW_LIBS) $(OPUS_LIBS)

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(PW_CFLAGS) $(OPUS_CFLAGS) -c -o $@ $<

# Dependencies (simplified)
audyn.o: audyn.c core/log.h core/frame_pool.h core/audio_queue.h core/ptp_clock.h \
         core/archive_policy.h core/level_meter.h core/vox.h sink/wav_sink.h \
         sink/opus_sink.h input/aes_input.h input/pipewire_input.h
core/log.o: core/log.c core/log.h
core/frame_pool.o: core/frame_pool.c core/frame_pool.h
core/audio_queue.o: core/audio_queue.c core/audio_queue.h core/frame_pool.h
core/ptp_clock.o: core/ptp_clock.c core/ptp_clock.h core/log.h
core/jitter_buffer.o: core/jitter_buffer.c core/jitter_buffer.h core/log.h
core/archive_policy.o: core/archive_policy.c core/archive_policy.h core/log.h
core/level_meter.o: core/level_meter.c core/level_meter.h core/frame_pool.h core/log.h
core/vox.o: core/vox.c core/vox.h core/frame_pool.h core/log.h
sink/wav_sink.o: sink/wav_sink.c sink/wav_sink.h core/log.h
sink/opus_sink.o: sink/opus_sink.c sink/opus_sink.h
input/pipewire_input.o: input/pipewire_input.c input/pipewire_input.h \
                        core/frame_pool.h core/audio_queue.h core/log.h
input/aes_input.o: input/aes_input.c input/aes_input.h \
                   core/frame_pool.h core/audio_queue.h core/log.h \
                   core/ptp_clock.h core/jitter_buffer.h

# Clean
clean:
	rm -f $(TARGET) $(OBJS)

# Install (requires root)
install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# Release build
release: CFLAGS += -DNDEBUG -O3
release: clean all

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@$(PKG_CONFIG) --exists libpipewire-0.3 && echo "  libpipewire-0.3: OK" || echo "  libpipewire-0.3: MISSING"
	@$(PKG_CONFIG) --exists opus && echo "  opus: OK" || echo "  opus: MISSING"
	@$(PKG_CONFIG) --exists ogg && echo "  ogg: OK" || echo "  ogg: MISSING"

# Help
help:
	@echo "Audyn Makefile targets:"
	@echo "  all        - Build audyn (default)"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall  - Remove from /usr/local/bin (requires sudo)"
	@echo "  debug      - Build with debug symbols, no optimization"
	@echo "  release    - Build optimized release version"
	@echo "  check-deps - Verify required libraries are installed"
	@echo "  help       - Show this help"
	@echo ""
	@echo "Dependencies: libpipewire-0.3-dev, libopus-dev, libogg-dev"

.PHONY: all clean install uninstall debug release check-deps help
