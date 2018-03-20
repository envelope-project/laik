TESTS= \
    test-vsum test-vsum-log test-vsum2 test-vsum3 \
    test-spmv test-spmv2 test-spmv2r \
    test-jac1d test-jac1d-repart \
    test-jac2d test-jac3d test-jac3dr \
    test-markov test-markov2 \
    test-propagation2d

-include ../Makefile.config

.PHONY: mpi tcp $(TESTS)

all: clean $(TESTS) $(TEST_SUBDIRS)

mpi:
	+$(MAKE) -C mpi

tcp:
	+$(MAKE) -C tcp

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
	$(SDIR)./test-jac1d-100-single.sh

test-jac1d-repart:
	$(SDIR)./test-jac1d-1000-repart-single.sh

test-jac2d:
	$(SDIR)./test-jac2d-1000-single.sh

test-jac3d:
	$(SDIR)./test-jac3d-100-single.sh

test-jac3dr:
	$(SDIR)./test-jac3dr-100-single.sh

test-markov:
	$(SDIR)./test-markov-20-4-single.sh

test-markov2:
	$(SDIR)./test-markov2-20-4-single.sh

test-propagation2d:
	$(SDIR)./test-propagation2d-10-single.sh

clean:
	rm -rf *.out

