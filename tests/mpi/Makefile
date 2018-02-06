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
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/vsum

test-vsum2:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/vsum2

test-vsum3:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/vsum3

test-spmv:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/spmv 4000

test-spmv2:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/spmv2 10 3000

test-spmv2r:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/spmv2 -r 10 3000

test-spmv2-shrink:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/spmv2 -s 2 10 3000

test-spmv2-shrink-inc:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/spmv2 -s 2 -i 10 3000

test-jac1d:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/jac1d 100

test-jac1d-repart:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/jac1d 1000 50 10

test-jac2d:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/jac2d -s 1000

test-jac2d-noc:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/jac2d -s -n 1000

test-jac3d:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/jac3d -s 100

test-jac3d-noc:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/jac3d -s -n 100

test-markov:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/markov 20 4

test-markov2:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/markov2 20 4

test-propagation2d:
	"$(SDIR)../run_example_test.sh" "mpi" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../../examples/propagation2d 10 10

clean:
	rm --force -- *.stdout *.stderr
