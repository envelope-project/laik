# same tests as in tests/, but using 4 MPI tasks

TESTS=test-vsum test-spmv test-spmv2

.PHONY: $(TESTS)

all: clean $(TESTS)

test-vsum:
	mpirun -np 4 ../../examples/vsum | sort > test-vsum.out
	cmp test-vsum.out test-vsum.expected

test-spmv:
	mpirun -np 4 ../../examples/spmv | sort > test-spmv.out
	cmp test-spmv.out test-spmv.expected

test-spmv2:
	mpirun -np 4 ../../examples/spmv2 | sort > test-spmv2.out
	cmp test-spmv2.out test-spmv2.expected

clean:
	rm -rf *.out

