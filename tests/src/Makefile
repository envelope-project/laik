# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../../Makefile.config

TESTBINS = kvstest locationtest anytest spacestest

LDFLAGS = $(OPT)
CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../../include
LAIKLIB = $(abspath ../../liblaik.so)

all: $(TESTBINS)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

kvstest: kvstest.o $(LAIKLIB)

locationtest: locationtest.o $(LAIKLIB)

anytest: anytest.o $(LAIKLIB)

spacestest: spacestest.o $(LAIKLIB)

clean:
	rm -f *.o *~ $(TESTBINS)
