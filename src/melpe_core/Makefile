# ============================================================================
# MELPe Codec - Simple Makefile
# NATO STANAG 4591 Voice Codec (600/1200/2400 bps)
# ============================================================================

CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -O2 -std=c99
ARFLAGS = rcs

# Output files
LIB = libmelpe.a
CLI = melpe_cli

# Core codec sources
CORE_SRC = melp_ana.c melp_syn.c melp_chn.c melp_sub.c \
           dsp_sub.c fft_lib.c fs_lib.c lpc_lib.c mat_lib.c \
           math_lib.c mathdp31.c mathhalf.c vq_lib.c \
           pitch.c pit_lib.c classify.c harm.c postfilt.c npp.c \
           qnt12.c qnt12_cb.c msvq_cb.c fsvq_cb.c coeff.c \
           fec_code.c global.c transcode.c

# 600 bps sources
SRC_600 = bitorder600.c cbk600_gain.c cbk600_msvq.c cbk600_qpit.c \
          cbk600_voicing.c lib600_bfi.c lib600_gain.c lib600_mode.c \
          lib600_msvq.c lib600_qpit.c lib600_rds.c lib600_srt.c \
          lib600_str.c lib600_transc.c lib600_voicing.c lib600_wrs.c \
          var600_bfi.c var600_gain.c var600_mode.c var600_msvq.c \
          var600_qpit.c var600_voicing.c

# API source
API_SRC = melpe_api.c

# CLI sources
CLI_SRC = sc6enc6.c main.c

# All library sources
LIB_SRC = $(CORE_SRC) $(SRC_600) $(API_SRC)
LIB_OBJ = $(LIB_SRC:.c=.o)
CLI_OBJ = $(CLI_SRC:.c=.o)

# Default target
all: $(LIB) $(CLI)

# Static library
$(LIB): $(LIB_OBJ)
	$(AR) $(ARFLAGS) $@ $^

# Command-line tool
$(CLI): $(CLI_OBJ) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(CLI_OBJ) -L. -lmelpe -lm

# Compile rule
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f *.o $(LIB) $(CLI)

# Install (adjust prefix as needed)
PREFIX ?= /usr/local
install: $(LIB)
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	cp $(LIB) $(PREFIX)/lib/
	cp melpe_api.h sc1200.h $(PREFIX)/include/

.PHONY: all clean install
