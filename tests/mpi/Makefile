# often same tests as in tests/, but using 4 MPI tasks

TESTS= \
    test-vsum test-vsum2 \
    test-spmv test-spmv2 test-spmv2r \
    test-spmv2-shrink test-spmv2-shrink-inc \
    test-jac1d test-jac1d-repart \
    test-jac2d test-jac3d \
    test-jac2d-noc test-jac3d-noc \
    test-markov

.PHONY: $(TESTS)

all: clean $(TESTS)

test-vsum:
	mpirun -np 4 ../../examples/vsum | sort > test-vsum.out
	cmp test-vsum.out test-vsum.expected

test-vsum2:
	mpirun -np 4 ../../examples/vsum2 | sort > test-vsum2.out
	cmp test-vsum2.out test-vsum2.expected

test-spmv:
	mpirun -np 4 ../../examples/spmv 4000 | sort > test-spmv.out
	cmp test-spmv.out test-spmv.expected

test-spmv2:
	mpirun -np 4 ../../examples/spmv2 10 3000 | sort > test-spmv2.out
	cmp test-spmv2.out test-spmv2.expected

test-spmv2r:
	mpirun -np 4 ../../examples/spmv2 -r 10 3000 | sort > test-spmv2r.out
	cmp test-spmv2r.out test-spmv2.expected

test-spmv2-shrink:
	mpirun -np 4 ../../examples/spmv2 -s 2 10 3000 | sort > test-spmv2s.out
	cmp test-spmv2s.out test-spmv2.expected

test-spmv2-shrink-inc:
	mpirun -np 4 ../../examples/spmv2 -s 2 -i 10 3000 | sort > test-spmv2si.out
	cmp test-spmv2si.out test-spmv2.expected

test-jac1d:
	mpirun -np 4 ../../examples/jac1d 100 | sort > test-jac1d-100.out
	cmp test-jac1d-100.out test-jac1d-100.expected

test-jac1d-repart:
	mpirun -np 4 ../../examples/jac1d 1000 50 10 | sort > test-jac1d-1000-repart.out
	cmp test-jac1d-1000-repart.out test-jac1d-1000-repart.expected

test-jac2d:
	mpirun -np 4 ../../examples/jac2d -s 1000 | sort > test-jac2d-1000.out
	cmp test-jac2d-1000.out test-jac2d-1000.expected

test-jac2d-noc:
	mpirun -np 4 ../../examples/jac2d -s -n 1000 | sort > test-jac2dn-1000.out
	cmp test-jac2dn-1000.out test-jac2dn-1000.expected

test-jac3d:
	mpirun -np 4 ../../examples/jac3d -s 100 | sort > test-jac3d-100.out
	cmp test-jac3d-100.out test-jac3d-100.expected

test-jac3d-noc:
	mpirun -np 4 ../../examples/jac3d -s -n 100 | sort > test-jac3dn-100.out
	cmp test-jac3dn-100.out test-jac3dn-100.expected

test-markov:
	mpirun -np 4 ../../examples/markov 40 4 | sort > test-markov-40-4.out
	cmp test-markov-40-4.out test-markov-40-4.expected


clean:
	rm -rf *.out

