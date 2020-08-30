# often same tests as in tests/, but using 1 and 4 MPI tasks

# local test config
-include ../../Makefile.config

TESTS= \
    test-vsum test-vsum2 \
    test-spmv test-spmv2 test-spmv2r \
    test-spmv2-shrink test-spmv2-shrink-inc \
    test-jac1d test-jac1d-repart \
    test-jac2d test-jac2d-noc test-jac2d-gen \
    test-jac3d test-jac3dr test-jac3d-noc test-jac3dr-noc \
    test-jac3de test-jac3der test-jac3da test-jac3dar \
    test-jac3dri test-jac3deri test-jac3dari test-jac3d-rgx3 \
    test-markov test-markov2 test-markov2-f \
    test-propagation2d test-propagation2do \
    test-kvstest test-location test-spaces

.PHONY: $(TESTS)

all: clean $(TESTS)

test-vsum:
	$(SDIR)./test-vsum-mpi-1.sh
	$(SDIR)./test-vsum-mpi-4.sh

test-vsum2:
	$(SDIR)./test-vsum2-mpi-1.sh
	$(SDIR)./test-vsum2-mpi-4.sh

test-spmv:
	$(SDIR)./test-spmv-mpi-1.sh
	$(SDIR)./test-spmv-mpi-4.sh

test-spmv2:
	$(SDIR)./test-spmv2-mpi-1.sh
	$(SDIR)./test-spmv2-mpi-4.sh

test-spmv2r:
	$(SDIR)./test-spmv2r-mpi-1.sh
	$(SDIR)./test-spmv2r-mpi-4.sh

test-spmv2-shrink:
	$(SDIR)./test-spmv2-shrink-mpi-4.sh

test-spmv2-shrink-inc:
	$(SDIR)./test-spmv2-shrink-inc-mpi-4.sh

test-jac1d:
	$(SDIR)./test-jac1d-100-mpi-1.sh
	$(SDIR)./test-jac1d-100-mpi-4.sh

test-jac1d-repart:
	$(SDIR)./test-jac1d-1000-repart-mpi-1.sh
	$(SDIR)./test-jac1d-1000-repart-mpi-4.sh

test-jac2d:
	$(SDIR)./test-jac2d-1000-mpi-1.sh
	$(SDIR)./test-jac2d-1000-mpi-4.sh

test-jac2d-noc:
	$(SDIR)./test-jac2dn-1000-mpi-4.sh

test-jac2d-gen:
	$(SDIR)./test-jac2d-gen-1000-mpi-4.sh

test-jac3d:
	$(SDIR)./test-jac3d-100-mpi-1.sh
	$(SDIR)./test-jac3d-100-mpi-4.sh

test-jac3dr:
	$(SDIR)./test-jac3dr-100-mpi-1.sh
	$(SDIR)./test-jac3dr-100-mpi-4.sh

test-jac3dri:
	$(SDIR)./test-jac3dri-100-mpi-4.sh

test-jac3d-rgx3:
	$(SDIR)./test-jac3d-rgx3-100-mpi-4.sh

test-jac3de:
	$(SDIR)./test-jac3de-100-mpi-1.sh
	$(SDIR)./test-jac3de-100-mpi-4.sh

test-jac3der:
	$(SDIR)./test-jac3der-100-mpi-1.sh
	$(SDIR)./test-jac3der-100-mpi-4.sh

test-jac3deri:
	$(SDIR)./test-jac3deri-100-mpi-4.sh

test-jac3da:
	$(SDIR)./test-jac3da-100-mpi-1.sh
	$(SDIR)./test-jac3da-100-mpi-4.sh

test-jac3dar:
	$(SDIR)./test-jac3dar-100-mpi-1.sh
	$(SDIR)./test-jac3dar-100-mpi-4.sh

test-jac3dari:
	$(SDIR)./test-jac3dari-100-mpi-4.sh

test-jac3d-noc:
	$(SDIR)./test-jac3dn-100-mpi-4.sh

test-jac3dr-noc:
	$(SDIR)./test-jac3dnr-100-mpi-4.sh

test-markov:
	$(SDIR)./test-markov-20-4-mpi-1.sh
	$(SDIR)./test-markov-40-4-mpi-4.sh

test-markov2:
	$(SDIR)./test-markov2-20-4-mpi-1.sh
	$(SDIR)./test-markov2-40-4-mpi-4.sh

test-markov2-f:
	$(SDIR)./test-markov2-f-500-5-mpi-4.sh

test-propagation2d:
	$(SDIR)./test-propagation2d-10-mpi-1.sh
	$(SDIR)./test-propagation2d-10-mpi-4.sh

test-propagation2do:
	$(SDIR)./test-propagation2do-10-mpi-4.sh

test-kvstest:
	$(SDIR)./test-kvstest-mpi-1.sh
	$(SDIR)./test-kvstest-mpi-4.sh

test-location:
	$(SDIR)./unit_tests/test-location-mpi-4.sh

test-spaces:
	$(SDIR)./unit_tests/test-spaces-mpi-4.sh

clean:
	rm -rf *.out

