CC = gcc
CFLAGS = -std=c11 -g -I../ -DZIDX_DEBUG
FILES = ../zidx.c
LDFLAGS = -lz

default: all

run_%: %
	./$<

all: test_stream test_read

%: %.c
	$(CC) $(CFLAGS) $(FILES) $< -o $@ $(LDFLAGS)

