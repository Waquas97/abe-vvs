# ===== Streaming client (default: in-process CPABE) =====

CC            := gcc
CSTD          := -std=c11
WARN          := -Wall -Wextra -Werror=implicit-function-declaration
OPT           := -O2

# Default to USE_CPABE_LIB=1 (in-process decryptor)
USE_CPABE_LIB ?= 1

# Core deps via pkg-config (libxml2, libcurl) -- both downloader.c and mpd_parser.c need curl
PKG_CFLAGS    := $(shell pkg-config --cflags libxml-2.0 libcurl)
PKG_LIBS      := $(shell pkg-config --libs   libxml-2.0 libcurl)

# Base flags
CFLAGS_BASE   := $(CSTD) $(WARN) $(OPT) $(PKG_CFLAGS)
CFLAGS        := $(CFLAGS_BASE)
LIBS          := $(PKG_LIBS) -lpthread

ifeq ($(USE_CPABE_LIB),1)
  CFLAGS += -DUSE_CPABE_LIB
endif

# --- CPABE integration (in-process) ---
CPABE_DIR     := src/cpabe
CPABE_SRCS    := $(CPABE_DIR)/common.c $(CPABE_DIR)/policy_lang.c
# Add more .c files here if your CPABE code needs them (e.g., $(CPABE_DIR)/bswabe.c)

# Headers & libs: pbc, gmp, openssl (crypto), glib-2.0
CPABE_CFLAGS  := -I/usr/local/include/pbc $(shell pkg-config --cflags glib-2.0) -I$(CPABE_DIR)
CPABE_LIBS    := -L/usr/local/lib -lbswabe -lpbc -lgmp -lcrypto $(shell pkg-config --libs glib-2.0)

# Some older systems need -lrt for clock_nanosleep/clock_gettime; uncomment if needed
# LIBS += -lrt

# Sources
SRC_DIR       := src
SRC           := $(wildcard $(SRC_DIR)/*.c)
OBJ           := $(patsubst %.c,%.o,$(SRC))

BIN           := stream_client

.PHONY: all clean

all: $(BIN)

# Link rule
$(BIN): $(OBJ)
ifeq ($(USE_CPABE_LIB),1)
	@echo ">> Linking with in-process CPABE (USE_CPABE_LIB=1)"
	$(CC) -o $@ $(OBJ) $(CPABE_SRCS) $(CFLAGS) $(CPABE_CFLAGS) $(LIBS) $(CPABE_LIBS) -DUSE_CPABE_LIB
else
	@echo ">> Linking WITHOUT in-process CPABE (external binary fallback)"
	$(CC) -o $@ $(OBJ) $(CFLAGS) $(LIBS)
endif

# Compile rule (quotes handle spaces in paths)
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
ifeq ($(USE_CPABE_LIB),1)
	$(CC) $(CFLAGS) $(CPABE_CFLAGS) -c $< -o $@
else
	$(CC) $(CFLAGS) -c $< -o $@
endif

clean:
	rm -f $(SRC_DIR)/*.o $(BIN)
