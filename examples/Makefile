# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../Makefile.config

EXAMPLES = vsum vsum2 spmv spmv2 \
    jac1d jac2d jac2d-ser jac3d \
    markov-ser markov markov2 \
    propagation1d propagation2d

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I$(SDIR)../include
LAIKLIB = $(abspath ../liblaik.so)

all: $(EXAMPLES)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

vsum: vsum.o $(LAIKLIB)

vsum2: vsum2.o $(LAIKLIB)

spmv: spmv.o $(LAIKLIB)

spmv2: $(SDIR)spmv2.c $(LAIKLIB)
	$(CC) $(CFLAGS) $(OMP_FLAGS) $< $(LAIKLIB) -o $@

jac1d: jac1d.o $(LAIKLIB)

jac2d: jac2d.o $(LAIKLIB)

jac3d: jac3d.o $(LAIKLIB)

markov: markov.o $(LAIKLIB)

markov2: markov2.o $(LAIKLIB)

propagation1d: $(SDIR)propagation1d.c $(LAIKLIB)
	$(CC) $(CFLAGS) $< $(LAIKLIB) -o $@ -lm

propagation2d: $(SDIR)propagation2d.c $(LAIKLIB)
	$(CC) $(CFLAGS) $< $(LAIKLIB) -o $@ -lm

clean:
	rm -f *.o *~ *.ppm $(EXAMPLES)
