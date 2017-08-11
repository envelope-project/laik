# often same tests as in tests/, but using 4 MPI tasks

TESTS= \
    test-vsum test-vsum2 \
    test-spmv test-spmv2 test-spmv2r \
    test-spmv2-shrink test-spmv2-shrink-inc \
    test-jac1d

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

clean:
	rm -rf *.out

