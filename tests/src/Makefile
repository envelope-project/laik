# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../../Makefile.config

TESTBINS = kvstest

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../../include
LAIKLIB = $(abspath ../../liblaik.so)

all: $(TESTBINS)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

kvstest: kvstest.o $(LAIKLIB)

clean:
	rm -f *.o *~ $(TESTBINS)
