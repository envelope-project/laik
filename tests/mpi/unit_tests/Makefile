# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../Makefile.config

UNIT_TESTS = core

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../include
LAIKLIB = ../../liblaik.so

all: $(UNIT_TESTS)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

core_location_utils: core_location_utils.o $(LAIKLIB)

clean:
	rm -f *.o *~ *.ppm $(UNIT_TESTS)
