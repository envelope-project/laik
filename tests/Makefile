TESTS= \
    test-vsum test-vsum2 test-vsum3 \
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
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/vsum

test-vsum2:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/vsum2

test-vsum3:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/vsum3

test-spmv:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/spmv 4000

test-spmv2:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/spmv2 10 3000

test-spmv2r:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/spmv2 -r 10 3000

test-jac1d:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/jac1d 100

test-jac1d-repart:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/jac1d 1000 50 10

test-jac2d:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/jac2d -s 1000

test-jac3d:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/jac3d -s 100

test-markov:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/markov 20 4

test-markov2:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/markov2 20 4

test-propagation2d:
	"$(SDIR)./run_example_test.sh" "single" "$(@).stdout" "$(@).stderr" "$(SDIR)$(@).expected" ../examples/propagation2d 10 10

clean:
	rm --force -- *.stdout *.stderr
