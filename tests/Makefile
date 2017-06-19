TESTS=test-vsum test-spmv test-spmv2

-include ../Makefile.config

.PHONY: mpi $(TESTS)

all: clean $(TESTS) $(TEST_SUBDIRS)

mpi:
	+$(MAKE) -C mpi

test-vsum:
	../examples/vsum > test-vsum.out
	cmp test-vsum.out test-vsum.expected

test-spmv:
	../examples/spmv > test-spmv.out
	cmp test-spmv.out test-spmv.expected

test-spmv2:
	../examples/spmv2 > test-spmv2.out
	cmp test-spmv2.out test-spmv2.expected

clean:
	rm -rf *.out

