# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../Makefile.config

TESTS = checkpoint checkpoint-jac2d checkpoint-jac2d-recovery checkpoint-mandelbrot-recovery checkpoint-partitioner

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../../include
LAIKLIB = $(abspath ../../liblaik.so)

all: $(TESTS)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

checkpoint: checkpoint.o $(LAIKLIB)
checkpoint-jac2d: checkpoint-jac2d.o $(LAIKLIB)
checkpoint-jac2d-recovery: checkpoint-jac2d-recovery.o $(LAIKLIB)
checkpoint-mandelbrot-recovery: checkpoint-mandelbrot-recovery.o $(LAIKLIB)
checkpoint-partitioner: checkpoint-partitioner.o $(LAIKLIB)

clean:
	rm -f *.o *~ *.ppm $(TESTS)
