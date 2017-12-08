# default settings
OPT=-g

# settings from 'configure', may overwrite defaults
-include ../Makefile.config

EXAMPLES = vsum vsum2 spmv spmv2 \
    jac1d jac2d jac2d-ser jac3d \
    markov-ser markov markov2 \
    propagation1d propagation2d

CFLAGS = $(OPT) $(WARN) $(DEFS) -std=gnu99 -I../include
LAIKLIB = $(abspath ../liblaik.so)

all: $(EXAMPLES)

vsum: vsum.o $(LAIKLIB)

vsum2: vsum2.o $(LAIKLIB)

spmv: spmv.o $(LAIKLIB)

spmv2: spmv2.c $(LAIKLIB)
	$(CC) $(CFLAGS) $(OMP_FLAGS) spmv2.c $(LAIKLIB) -o spmv2

jac1d: jac1d.c $(LAIKLIB)
	$(CC) $(CFLAGS) $(OMP_FLAGS) jac1d.c $(LAIKLIB) -o jac1d

jac2d: jac2d.c $(LAIKLIB)
	$(CC) $(CFLAGS) $(OMP_FLAGS) jac2d.c $(LAIKLIB) -o jac2d

jac3d: jac3d.c $(LAIKLIB)
	$(CC) $(CFLAGS) $(OMP_FLAGS) jac3d.c $(LAIKLIB) -o jac3d

markov: markov.o $(LAIKLIB)
	$(CC) $(CFLAGS) markov.o $(LAIKLIB) -o markov

markov2: markov2.o $(LAIKLIB)
	$(CC) $(CFLAGS) markov2.o $(LAIKLIB) -o markov2

propagation1d: propagation1d.c $(LAIKLIB)
	$(CC) $(CFLAGS) propagation1d.c $(LAIKLIB) -o propagation1d -lm

propagation2d: propagation2d.c $(LAIKLIB)
	$(CC) $(CFLAGS) propagation2d.c $(LAIKLIB)  -o propagation2d -lm

clean:
	rm -f *.o *~ *.ppm $(EXAMPLES)
