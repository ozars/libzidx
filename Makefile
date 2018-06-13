CC      := gcc
SONAME  := libzidx.so.1
CFLAGS  := -std=c11 -g -DZIDX_DEBUG -shared -fPIC -Wl,-soname,$(SONAME)
FILES   := zidx.c
DEPS    := zidx.h
LDFLAGS := -lz -lc

SRC_DIR := src
OUT_DIR := bin

ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
SRC_DIR  := $(ROOT_DIR)/$(SRC_DIR)
OUT_DIR  := $(ROOT_DIR)/$(OUT_DIR)
OUT_FILE := $(OUT_DIR)/$(SONAME)

FILES := $(addprefix $(SRC_DIR)/,$(FILES))
DEPS  := $(addprefix $(SRC_DIR)/,$(DEPS)) $(FILES)

default: all

all: $(DEPS)
	mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) $(FILES) -o $(OUT_FILE) $(LDFLAGS)
