include ../Makefile.common

CFLAGS   += -I '../include'
CXXFLAGS += -I '../include'
LDLIBS   += '../liblaik.so'

EXAMPLES = vsum vsum2 spmv spmv2 \
    jac1d jac2d jac2d-ser jac3d \
    markov-ser markov markov2 \
    raytracer \
    propagation1d propagation2d

all: $(EXAMPLES)

vsum: vsum.o

vsum2: vsum2.o

spmv: spmv.o

spmv2: spmv2.o

jac1d: jac1d.o

jac2d: jac2d.o

jac3d: jac3d.o

markov: markov.o

markov2: markov2.o

raytracer: LDLIBS += -lm -lstdc++
raytracer: raytracer.o

propagation1d: LDLIBS += -lm
propagation1d: propagation1d.o

propagation2d: LDLIBS += -lm
propagation2d: propagation2d.o

clean:
	rm -f *.o *~ *.ppm $(EXAMPLES)
