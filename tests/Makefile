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
	$(SDIR)./test-vsum-single.sh

test-vsum-log:
	$(SDIR)./test-vsum-log-single.sh

test-vsum2:
	$(SDIR)./test-vsum2-single.sh

test-vsum3:
	$(SDIR)./test-vsum3-single.sh

test-spmv:
	$(SDIR)./test-spmv-single.sh

test-spmv2:
	$(SDIR)./test-spmv2-single.sh

test-spmv2r:
	$(SDIR)./test-spmv2r-single.sh

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

