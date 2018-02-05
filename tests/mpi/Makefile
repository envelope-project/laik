# often same tests as in tests/, but using 4 MPI tasks

TESTS= \
    test-vsum test-vsum2 test-vsum3 \
    test-spmv test-spmv2 test-spmv2r \
    test-spmv2-shrink test-spmv2-shrink-inc \
    test-jac1d test-jac1d-repart \
    test-jac2d test-jac3d \
    test-jac2d-noc test-jac3d-noc \
    test-markov test-markov2 \
    test-propagation2d

.PHONY: $(TESTS)

all: clean $(TESTS)

test-vsum:
	mpirun -np 4 ../../examples/vsum | sort > test-vsum.out
	cmp test-vsum.out $(SDIR)test-vsum.expected

test-vsum2:
	mpirun -np 4 ../../examples/vsum2 | sort > test-vsum2.out
	cmp test-vsum2.out $(SDIR)test-vsum2.expected

test-vsum3:
	mpirun -np 4 ../../examples/vsum3 | sort > test-vsum3.out
	cmp test-vsum3.out $(SDIR)test-vsum.expected

test-spmv:
	mpirun -np 4 ../../examples/spmv 4000 | sort > test-spmv.out
	cmp test-spmv.out $(SDIR)test-spmv.expected

test-spmv2:
	mpirun -np 4 ../../examples/spmv2 10 3000 | sort > test-spmv2.out
	cmp test-spmv2.out $(SDIR)test-spmv2.expected

test-spmv2r:
	mpirun -np 4 ../../examples/spmv2 -r 10 3000 | sort > test-spmv2r.out
	cmp test-spmv2r.out $(SDIR)test-spmv2.expected

test-spmv2-shrink:
	mpirun -np 4 ../../examples/spmv2 -s 2 10 3000 | sort > test-spmv2s.out
	cmp test-spmv2s.out $(SDIR)test-spmv2.expected

test-spmv2-shrink-inc:
	mpirun -np 4 ../../examples/spmv2 -s 2 -i 10 3000 | sort > test-spmv2si.out
	cmp test-spmv2si.out $(SDIR)test-spmv2.expected

test-jac1d:
	mpirun -np 4 ../../examples/jac1d 100 > test-jac1d-100.out
	cmp test-jac1d-100.out $(SDIR)test-jac1d-100.expected

test-jac1d-repart:
	mpirun -np 4 ../../examples/jac1d 1000 50 10 > test-jac1d-1000-repart.out
	cmp test-jac1d-1000-repart.out $(SDIR)test-jac1d-1000-repart.expected

test-jac2d:
	mpirun -np 4 ../../examples/jac2d -s 1000 > test-jac2d-1000.out
	cmp test-jac2d-1000.out $(SDIR)test-jac2d-1000.expected

test-jac2d-noc:
	mpirun -np 4 ../../examples/jac2d -s -n 1000 > test-jac2dn-1000.out
	cmp test-jac2dn-1000.out $(SDIR)test-jac2dn-1000.expected

test-jac3d:
	mpirun -np 4 ../../examples/jac3d -s 100 > test-jac3d-100.out
	cmp test-jac3d-100.out $(SDIR)test-jac3d-100.expected

test-jac3d-noc:
	mpirun -np 4 ../../examples/jac3d -s -n 100 > test-jac3dn-100.out
	cmp test-jac3dn-100.out $(SDIR)test-jac3dn-100.expected

test-markov:
	mpirun -np 4 ../../examples/markov 40 4 > test-markov-40-4.out
	cmp test-markov-40-4.out $(SDIR)test-markov-40-4.expected

test-markov2:
	mpirun -np 4 ../../examples/markov2 40 4 > test-markov2-40-4.out
	cmp test-markov2-40-4.out $(SDIR)test-markov2-40-4.expected

test-propagation2d:
	mpirun -np 4 ../../examples/propagation2d 10 10 > test-propagation2d-10.out
	cmp test-propagation2d-10.out $(SDIR)test-propagation2d-10.expected


clean:
	rm -rf *.out

