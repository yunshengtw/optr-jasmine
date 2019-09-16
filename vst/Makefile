CC = gcc

VST_SRC = ./src/*.c
VST_CFLAGS = -std=c99 -g -O2 -pthread -Wall -rdynamic -I. -I./src -I./include -DVST
VST_LDFLAGS = -pthread -ldl -T ld_script

FTL_ROOT = ../ftl_optr
FTL_SRC = ${FTL_ROOT}/*.c ./port.c
FTL_CFLAGS = -shared -std=c99 -g -O2 -fPIC -I${FTL_ROOT} -I. -I./src -I./include -DVST

all: vst-jasmine ftl.so
.PHONY: all

clean:
	rm -f vst-jasmine ftl.so
.PHONY: clean

vst-jasmine: $(VST_SRC)
	$(CC) $(VST_CFLAGS) $^ $(VST_LDFLAGS) -o $@

ftl.so: $(FTL_SRC)
	$(CC) $(FTL_CFLAGS) $^ -o $@
