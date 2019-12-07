# often same tests as in tests/, but using 1 and 4 MPI tasks

# local test config
-include ../../Makefile.config

TESTS= \
    test-jac2d-1000-ft. test-lulesh-ft

.PHONY: $(TESTS)

all: clean $(TESTS)

test-jac2d-1000-ft:
	$(SDIR)./test-jac2d-1000-ft.sh

test-lulesh-ft:
	$(SDIR)./test-lulesh-ft.sh

clean:
	rm -rf *.out

