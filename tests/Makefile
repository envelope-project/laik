TESTS= \
    test-vsum test-vsum-log test-vsum2 test-vsum3 \
    test-spmv test-spmv2 test-spmv2r \
    test-jac1d test-jac1d-repart \
    test-jac2d test-jac3d \
    test-markov test-markov2 \
    test-propagation2d

-include ../Makefile.config

.PHONY: mpi $(TESTS)

all: clean $(TESTS) $(TEST_SUBDIRS)

mpi:
	+$(MAKE) -C mpi

test-vsum:
	../examples/vsum > test-vsum.out
	cmp test-vsum.out $(SDIR)test-vsum.expected

test-vsum-log:
	LAIK_LOG=1:0 ../examples/vsum > test-vsum.out 2>/dev/null
	cmp test-vsum.out $(SDIR)test-vsum.expected

test-vsum2:
	../examples/vsum2 > test-vsum2.out
	cmp test-vsum2.out $(SDIR)test-vsum.expected

test-vsum3:
	../examples/vsum > test-vsum3.out
	cmp test-vsum3.out $(SDIR)test-vsum.expected

test-spmv:
	../examples/spmv 4000 > test-spmv.out
	cmp test-spmv.out $(SDIR)test-spmv.expected

test-spmv2:
	../examples/spmv2 10 3000 > test-spmv2.out
	cmp test-spmv2.out $(SDIR)test-spmv2.expected

test-spmv2r:
	../examples/spmv2 -r 10 3000 > test-spmv2r.out
	cmp test-spmv2r.out $(SDIR)test-spmv2.expected

test-jac1d:
	../examples/jac1d 100 > test-jac1d-100.out
	cmp test-jac1d-100.out $(SDIR)test-jac1d-100.expected

test-jac1d-repart:
	../examples/jac1d 1000 50 10 > test-jac1d-1000-repart.out
	cmp test-jac1d-1000-repart.out $(SDIR)test-jac1d-1000-repart.expected

test-jac2d:
	../examples/jac2d -s 1000 > test-jac2d-1000.out
	cmp test-jac2d-1000.out $(SDIR)test-jac2d-1000.expected

test-jac3d:
	../examples/jac3d -s 100 > test-jac3d-100.out
	cmp test-jac3d-100.out $(SDIR)test-jac3d-100.expected

test-markov:
	../examples/markov 20 4 > test-markov-20-4.out
	cmp test-markov-20-4.out $(SDIR)test-markov-20-4.expected

test-markov2:
	../examples/markov2 20 4 > test-markov2-20-4.out
	cmp test-markov2-20-4.out $(SDIR)test-markov2-20-4.expected

test-propagation2d:
	../examples/propagation2d 10 10 > test-propagation2d-10.out
	cmp test-propagation2d-10.out $(SDIR)test-propagation2d-10.expected

clean:
	rm -rf *.out

