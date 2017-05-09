EXAMPLES = vsum spmv spmv2
CFLAGS = -g -std=gnu99 -I../include
CFLAGS += $(DEFS)

all: $(EXAMPLES)

vsum: vsum.o ../liblaik.a

spmv: spmv.o ../liblaik.a

spmv2: spmv2.o ../liblaik.a

clean:
	rm -f *.o *~ $(EXAMPLES)
