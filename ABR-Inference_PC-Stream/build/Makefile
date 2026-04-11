# ===== Streaming client (with CPABE and ML Inference) =====

CC            := gcc
CSTD          := -std=c11
WARN          := -Wall -Wextra -Werror=implicit-function-declaration
OPT           := -O3 -fno-lto -fno-use-linker-plugin

# Default to USE_CPABE_LIB=1 (in-process decryptor)
USE_CPABE_LIB ?= 1

# 1. PATHS: Explicitly point to Conda to prevent "System Leaks"
CONDA_INC     := $(CONDA_PREFIX)/include
CONDA_LIB     := $(CONDA_PREFIX)/lib

# 2. PYTHON & ML FLAGS (Extracted from your working command)
# We strip -flto from ldflags to prevent the "bytecode stream" error
PY_CFLAGS     := $(shell python3-config --cflags) -I$(shell python3 -c "import numpy; print(numpy.get_include())")
PY_LDFLAGS := $(shell python3-config --ldflags --embed | sed 's/-flto//g')
ML_LIBS       := -lcrypt -ldl -lutil -lm

# 3. CORE DEPS (libxml2, libcurl, glib-2.0)
# We force the include paths to the Conda environment
PKG_CFLAGS    := -I$(CONDA_INC)/libxml2 -I$(CONDA_INC) $(shell pkg-config --cflags glib-2.0) -I/usr/include/libxml2
PKG_LIBS      := -L$(CONDA_LIB) -lxml2 -lcurl -lz $(shell pkg-config --libs glib-2.0)

# 4. CPABE integration
CPABE_DIR     := src/cpabe
CPABE_SRCS    := $(CPABE_DIR)/common.c $(CPABE_DIR)/policy_lang.c
CPABE_CFLAGS  := -I$(CONDA_INC)/pbc -I$(CPABE_DIR)
CPABE_LIBS    := -L/usr/local/lib -L$(CONDA_LIB) -lbswabe -lpbc -lgmp -lcrypto -lssl

# Final Flag Aggregation
CFLAGS        := $(CSTD) $(WARN) $(OPT) $(PKG_CFLAGS) $(PY_CFLAGS)
ifeq ($(USE_CPABE_LIB),1)
    CFLAGS += -DUSE_CPABE_LIB $(CPABE_CFLAGS)
endif

# Linking Flags
LIBS          := $(PKG_LIBS) $(PY_LDFLAGS) $(ML_LIBS) -lpthread
ifeq ($(USE_CPABE_LIB),1)
    LIBS += $(CPABE_LIBS)
endif

# Runtime Path: Ensures the binary finds Conda libs when executed
LDFLAGS       := -Wl,-rpath,$(CONDA_LIB)

# Sources
SRC_DIR       := src
SRC           := $(wildcard $(SRC_DIR)/*.c)
OBJ           := $(patsubst %.c,%.o,$(SRC))

BIN           := stream_client

.PHONY: all clean

all: $(BIN)

# Link rule
$(BIN): $(OBJ)
	@echo ">> Linking $(BIN) with CPABE and ML support"
	$(CC) $(OPT) -o $@ $(OBJ) $(CPABE_SRCS) $(CFLAGS) $(LDFLAGS) $(LIBS)

# Compile rule
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRC_DIR)/*.o $(BIN)
