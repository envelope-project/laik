# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../Makefile.config

TESTS = checkpoint

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../../include
LAIKLIB = ../../liblaik.so

all: $(TESTS)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

checkpoint: checkpoint.o $(LAIKLIB) -lssl -lcrypto

clean:
	rm -f *.o *~ *.ppm $(EXAMPLES)
