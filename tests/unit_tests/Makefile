# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../Makefile.config

UNIT_TESTS = core

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../include
LAIKLIB = $(abspath ../../liblaik.so)

all: $(UNIT_TESTS)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

core: core.o $(LAIKLIB)

clean:
	rm -f *.o *~ *.ppm $(UNIT_TESTS)
