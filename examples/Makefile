EXAMPLES = vsum spmv
CFLAGS = -g -std=gnu99 -I../include
CFLAGS += $(DEFS)

all: $(EXAMPLES)

vsum: vsum.o ../liblaik.a

spmv: spmv.o ../liblaik.a

clean:
	rm -f *.o *~ $(EXAMPLES)
