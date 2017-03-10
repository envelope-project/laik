EXAMPLES = vsum
CFLAGS = -g -std=gnu99 -I../include
CFLAGS += $(DEFS)

all: $(EXAMPLES)

vsum: vsum.o ../liblaik.a

clean:
	rm -f *.o *~ $(EXAMPLES)
