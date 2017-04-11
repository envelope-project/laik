# default installation path
PREFIX=/usr/local

# enable MPI backend
CC=mpicc
DEFS=-DLAIK_USEMPI

DEFS += -DLAIK_DEBUG

OPT = -g
CFLAGS=$(OPT) -std=gnu99 -Iinclude
LDFLAGS=$(OPT)

SRCS = $(wildcard src/*.c)
HEADERS = $(wildcard include/*.h include/laik/*.h)
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

# instruct GCC to produce dependency files
CFLAGS += -MMD -MP

CFLAGS += $(DEFS)

SUBDIRS=examples mqtt
.PHONY: $(SUBDIRS)

all: liblaik.a $(SUBDIRS)

liblaik.a: $(OBJS)
	ar rcs liblaik.a $(OBJS)

examples: liblaik.a
	cd examples && $(MAKE) CC=$(CC) DEFS='$(DEFS)'

mqtt: liblaik.a
	cd external/MQTT && $(MAKE) CC=$(CC) DEFS='$(DEFS)'

clean:
	rm -f *~ *.o $(OBJS) $(DEPS) liblaik.a
	cd examples && make clean
	cd external/MQTT && make clean

install: liblaik.a $(HEADERS)
	cp $(wildcard include/*.h) $(PREFIX)/include
	mkdir -p $(PREFIX)/include/laik
	cp $(wildcard include/laik/*.h) $(PREFIX)/include/laik
	mkdir -p $(PREFIX)/lib
	cp liblaik.a $(PREFIX)/lib

uninstall:
	rm -rf $(PREFIX)/include/laik
	rm -f $(PREFIX)/include/laik.h
	rm -f $(PREFIX)/include/laik-internal.h
	rm -f $(PREFIX)/include/laik-backend-*.h
	rm -f $(PREFIX)/lib/liblaik.a

# include previously generated dependency rules if existing
-include $(DEPS)
