TESTS= \
    test-vsum test-vsum-log test-vsum2 \
    test-spmv test-spmv2 test-spmv2r \
    test-jac1d

-include ../Makefile.config

.PHONY: mpi $(TESTS)

all: clean $(TESTS) $(TEST_SUBDIRS)

mpi:
	+$(MAKE) -C mpi

test-vsum:
	../examples/vsum > test-vsum.out
	cmp test-vsum.out test-vsum.expected

test-vsum-log:
	LAIK_LOG=1:0 ../examples/vsum > test-vsum.out 2>/dev/null
	cmp test-vsum.out test-vsum.expected

test-vsum2:
	../examples/vsum2 > test-vsum2.out
	cmp test-vsum2.out test-vsum.expected

test-spmv:
	../examples/spmv 4000 > test-spmv.out
	cmp test-spmv.out test-spmv.expected

test-spmv2:
	../examples/spmv2 10 3000 > test-spmv2.out
	cmp test-spmv2.out test-spmv2.expected

test-spmv2r:
	../examples/spmv2 -r 10 3000 > test-spmv2r.out
	cmp test-spmv2r.out test-spmv2.expected

test-jac1d:
	../examples/jac1d 100 > test-jac1d-100.out
	cmp test-jac1d-100.out test-jac1d-100.expected

clean:
	rm -rf *.out

